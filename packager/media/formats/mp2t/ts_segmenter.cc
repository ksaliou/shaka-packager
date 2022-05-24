// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/mp2t/ts_segmenter.h"

#include <memory>

#include "packager/media/base/audio_stream_info.h"
#include "packager/media/base/muxer_util.h"
#include "packager/media/base/video_stream_info.h"
#include "packager/media/event/muxer_listener.h"
#include "packager/media/formats/mp2t/pes_packet.h"
#include "packager/media/formats/mp2t/program_map_table_writer.h"
#include "packager/status.h"
#include "packager/status_macros.h"

namespace shaka {
namespace media {
namespace mp2t {

namespace {
const double kTsTimescale = 90000;

const uint8_t kPaddings[] = {
    0x10,
    0x0c,
    0x08,
    0x04
};

bool IsAudioCodec(Codec codec) {
  return codec >= kCodecAudio && codec < kCodecAudioMaxPlusOne;
}

bool IsVideoCodec(Codec codec) {
  return codec >= kCodecVideo && codec < kCodecVideoMaxPlusOne;
}

}  // namespace

TsSegmenter::TsSegmenter(const MuxerOptions& options, MuxerListener* listener)
    : muxer_options_(options),
      listener_(listener),
      transport_stream_timestamp_offset_(
          options.transport_stream_timestamp_offset_ms * kTsTimescale / 1000),
      pes_packet_generator_(
          new PesPacketGenerator(transport_stream_timestamp_offset_)) {}

TsSegmenter::~TsSegmenter() {}

Status TsSegmenter::Initialize(const StreamInfo& stream_info) {
  if (!pes_packet_generator_->Initialize(stream_info)) {
    return Status(error::MUXER_FAILURE,
                  "Failed to initialize PesPacketGenerator.");
  }

  const StreamType stream_type = stream_info.stream_type();
  if (stream_type != StreamType::kStreamVideo &&
      stream_type != StreamType::kStreamAudio) {
    LOG(ERROR) << "TsWriter cannot handle stream type " << stream_type
               << " yet.";
    return Status(error::MUXER_FAILURE, "Unsupported stream type.");
  }

  codec_ = stream_info.codec();
  if (stream_type == StreamType::kStreamAudio)
    audio_codec_config_ = stream_info.codec_config();

  timescale_scale_ = kTsTimescale / stream_info.time_scale();

  return DoInitialize();
}

Status TsSegmenter::Finalize() {
  return DoFinalize();
}

Status TsSegmenter::AddSample(const MediaSample& sample) {
  if (!ts_writer_) {
    std::unique_ptr<ProgramMapTableWriter> pmt_writer;
    if (codec_ == kCodecAC3) {
      // https://goo.gl/N7Tvqi MPEG-2 Stream Encryption Format for HTTP Live
      // Streaming 2.3.2.2 AC-3 Setup: For AC-3, the setup_data in the
      // audio_setup_information is the first 10 bytes of the audio data (the
      // syncframe()).
      // For unencrypted AC3, the setup_data is not used, so what is in there
      // does not matter.
      const size_t kSetupDataSize = 10u;
      if (sample.data_size() < kSetupDataSize) {
        LOG(ERROR) << "Sample is too small for AC3: " << sample.data_size();
        return Status(error::MUXER_FAILURE, "Sample is too small for AC3.");
      }
      const std::vector<uint8_t> setup_data(sample.data(),
                                            sample.data() + kSetupDataSize);
      pmt_writer.reset(new AudioProgramMapTableWriter(codec_, setup_data));
    } else if (IsAudioCodec(codec_)) {
      pmt_writer.reset(
          new AudioProgramMapTableWriter(codec_, audio_codec_config_));
    } else {
      DCHECK(IsVideoCodec(codec_));
      pmt_writer.reset(new VideoProgramMapTableWriter(codec_));
    }
    ts_writer_.reset(new TsWriter(std::move(pmt_writer)));
  }

  if (sample.is_encrypted())
    ts_writer_->SignalEncrypted();

  if (!segment_started_ && !sample.is_key_frame())
    LOG(WARNING) << "A segment will start with a non key frame.";

  if (!pes_packet_generator_->PushSample(sample)) {
    return Status(error::MUXER_FAILURE,
                  "Failed to add sample to PesPacketGenerator.");
  }
  return WritePesPackets();
}

void TsSegmenter::InjectTsWriterForTesting(std::unique_ptr<TsWriter> writer) {
  ts_writer_ = std::move(writer);
}

void TsSegmenter::InjectPesPacketGeneratorForTesting(
    std::unique_ptr<PesPacketGenerator> generator) {
  pes_packet_generator_ = std::move(generator);
}

void TsSegmenter::SetSegmentStartedForTesting(bool value) {
  segment_started_ = value;
}

Status TsSegmenter::StartSegmentIfNeeded(int64_t next_pts) {
  if (segment_started_)
    return Status::OK;
  segment_start_timestamp_ = next_pts;
  if (!ts_writer_->NewSegment(&segment_buffer_))
    return Status(error::MUXER_FAILURE, "Failed to initialize new segment.");
  segment_started_ = true;
  return Status::OK;
}

double TsSegmenter::GetDuration() const {
  return 0 * timescale_scale_;
}

Status TsSegmenter::WritePesPackets() {
  while (pes_packet_generator_->NumberOfReadyPesPackets() > 0u) {
    std::unique_ptr<PesPacket> pes_packet =
        pes_packet_generator_->GetNextPesPacket();

    Status status = StartSegmentIfNeeded(pes_packet->pts());
    if (!status.ok())
      return status;

    if (listener_ && IsVideoCodec(codec_) && pes_packet->is_key_frame()) {

      uint64_t start_pos = segment_buffer_.Size();
      const int64_t timestamp = pes_packet->pts();
      if (!ts_writer_->AddPesPacket(std::move(pes_packet), &segment_buffer_))
        return Status(error::MUXER_FAILURE, "Failed to add PES packet.");

      uint64_t end_pos = segment_buffer_.Size();

      listener_->OnKeyFrame(timestamp, start_pos, end_pos - start_pos);
    } else {
      if (!ts_writer_->AddPesPacket(std::move(pes_packet), &segment_buffer_))
        return Status(error::MUXER_FAILURE, "Failed to add PES packet.");
    }
  }
  return Status::OK;
}

Status TsSegmenter::FinalizeSegment(int64_t start_timestamp, int64_t duration) {
  if (!pes_packet_generator_->Flush()) {
    return Status(error::MUXER_FAILURE, "Failed to flush PesPacketGenerator.");
  }

  Status status = WritePesPackets();
  if (!status.ok())
    return status;

  // This method may be called from Finalize() so segment_started_ could
  // be false.
  if (!segment_started_)
    return Status::OK;

  const int64_t file_size = segment_buffer_.Size();
  segment_number_++;
  
  AddHls32Padding(file_size);

  status = ProcessSegmentBuffer();
  if(!status.ok())
    return status;

  std::string segment_path = GetSegmentPath();

  if (listener_) {
    listener_->OnNewSegment(segment_path,
                            start_timestamp * timescale_scale_ +
                                transport_stream_timestamp_offset_,
                            duration * timescale_scale_, file_size);
  }
  segment_started_ = false;

  return Status::OK;
}

Status TsSegmenter::AddHls32Padding(int64_t file_size)
{
  // HLS32
  int8_t stuffing_packet_count = 0, padding_bytes_count = 0, padding_bytes_index = 0;
  switch ( file_size % 32 )
  {
    case 0:
      stuffing_packet_count = 4;
      padding_bytes_count = 16;
      padding_bytes_index = 0;
      break;
    case 4:
      stuffing_packet_count = 5;
      padding_bytes_count = 16;
      padding_bytes_index = 0;
      break;
    case 8:
      stuffing_packet_count = 6;
      padding_bytes_count = 16;
      padding_bytes_index = 0;
      break;
    case 12:
      stuffing_packet_count = 7;
      padding_bytes_count = 16;
      padding_bytes_index = 0;
      break;
    case 16:
      stuffing_packet_count = 0;
      padding_bytes_count = 16;
      padding_bytes_index = 0;
      break;
    case 20:
      stuffing_packet_count = 0;
      padding_bytes_count = 12;
      padding_bytes_index = 1;
      break;
    case 24:
      stuffing_packet_count = 0;
      padding_bytes_count = 8;
      padding_bytes_index = 2;
      break;
    case 28:
      stuffing_packet_count = 0;
      padding_bytes_count = 4;
      padding_bytes_index = 3;
      break;
    default:
      stuffing_packet_count = 0;
      padding_bytes_count = 0;
    break;
  }

  for (int i = 0; i < stuffing_packet_count; i++) {
    if(!ts_writer_->AddStuffingPacket(&segment_buffer_))
      return Status(error::MUXER_FAILURE, "Failed to add stuffing packet.");
  }

  for (int i = 0; i < padding_bytes_count; i++) {
    segment_buffer_.AppendInt(kPaddings[padding_bytes_index]);
  }

  return Status::OK;
}

}  // namespace mp2t
}  // namespace media
}  // namespace shaka
