// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/mp2t/ts_muxer.h"

#include "packager/media/formats/mp2t/ts_multi_file_segmenter.h"
#include "packager/media/formats/mp2t/ts_single_file_segmenter.h"

namespace shaka {
namespace media {
namespace mp2t {

namespace {
const int32_t kTsTimescale = 90000;
}  // namespace

TsMuxer::TsMuxer(const MuxerOptions& muxer_options) : Muxer(muxer_options) {}
TsMuxer::~TsMuxer() {}

Status TsMuxer::InitializeMuxer() {
  if (streams().size() > 1u)
    return Status(error::MUXER_FAILURE, "Cannot handle more than one streams.");

  if(options().segment_template.empty()) {
    segmenter_.reset(new TsSingleFileSegmenter(options(), muxer_listener()));
  } else {
    segmenter_.reset(new TsMultiFileSegmenter(options(), muxer_listener()));
  }

  Status status = segmenter_->Initialize(*streams()[0]);
  FireOnMediaStartEvent();
  return status;
}

Status TsMuxer::Finalize() {
  FireOnMediaEndEvent();
  return segmenter_->Finalize();
}

Status TsMuxer::AddMediaSample(size_t stream_id, const MediaSample& sample) {
  DCHECK_EQ(stream_id, 0u);
  if (num_samples_ < 2) {
    sample_durations_[num_samples_] =
        sample.duration() * kTsTimescale / streams().front()->time_scale();
    if (num_samples_ == 1 && muxer_listener())
      muxer_listener()->OnSampleDurationReady(sample_durations_[num_samples_]);
    num_samples_++;
  }
  return segmenter_->AddSample(sample);
}

Status TsMuxer::FinalizeSegment(size_t stream_id,
                                const SegmentInfo& segment_info) {
  DCHECK_EQ(stream_id, 0u);
  return segment_info.is_subsegment
             ? Status::OK
             : segmenter_->FinalizeSegment(segment_info.start_timestamp,
                                           segment_info.duration);
}

void TsMuxer::FireOnMediaStartEvent() {
  if (!muxer_listener())
    return;
  muxer_listener()->OnMediaStart(options(), *streams().front(), kTsTimescale,
                                 MuxerListener::kContainerMpeg2ts);
}

void TsMuxer::FireOnMediaEndEvent() {
  if (!muxer_listener())
    return;

  // For now, there is no single file TS segmenter. So all the values passed
  // here are left empty.

  //MuxerListener::MediaRanges range;
  //muxer_listener()->OnMediaEnd(range, 0);

  MuxerListener::MediaRanges media_range;
  media_range.init_range = GetInitRangeStartAndEnd();
  media_range.index_range = GetIndexRangeStartAndEnd();
  media_range.subsegment_ranges = segmenter_->GetSegmentRanges();

  const float duration_seconds = static_cast<float>(segmenter_->GetDuration());
  muxer_listener()->OnMediaEnd(media_range, duration_seconds);
}

base::Optional<Range> TsMuxer::GetInitRangeStartAndEnd() {
  size_t range_offset = 0;
  size_t range_size = 0;
  const bool has_range = segmenter_->GetInitRange(&range_offset, &range_size);

  if (!has_range)
    return base::nullopt;

  Range range;
  SetStartAndEndFromOffsetAndSize(range_offset, range_size, &range);
  return range;
}

base::Optional<Range> TsMuxer::GetIndexRangeStartAndEnd() {
  size_t range_offset = 0;
  size_t range_size = 0;
  const bool has_range = segmenter_->GetIndexRange(&range_offset, &range_size);

  if (!has_range)
    return base::nullopt;

  Range range;
  SetStartAndEndFromOffsetAndSize(range_offset, range_size, &range);
  return range;
}

void TsMuxer::SetStartAndEndFromOffsetAndSize(size_t offset,
                                     size_t size,
                                     Range* range) {
  DCHECK(range);
  range->start = static_cast<uint32_t>(offset);
  // Note that ranges are inclusive. So we need - 1.
  range->end = range->start + static_cast<uint32_t>(size) - 1;
}

}  // namespace mp2t
}  // namespace media
}  // namespace shaka
