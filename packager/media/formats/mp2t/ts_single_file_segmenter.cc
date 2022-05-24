// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/mp2t/ts_single_file_segmenter.h"

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

TsSingleFileSegmenter::TsSingleFileSegmenter(const MuxerOptions& options, MuxerListener* listener)
  : TsSegmenter(options, listener) {}

TsSingleFileSegmenter::~TsSingleFileSegmenter() {}

bool TsSingleFileSegmenter::GetInitRange(size_t* offset, size_t* size) {
  // In Finalize, ftyp and moov gets written first so offset must be 0.
  *offset = 0;
  *size = 0;
  return true;
}

bool TsSingleFileSegmenter::GetIndexRange(size_t* offset, size_t* size) {
  // Index range is right after init range so the offset must be the size of
  // ftyp and moov.
  *offset = 0;
  *size = 0;
  return true;
}

std::vector<Range> TsSingleFileSegmenter::GetSegmentRanges() {
  return ranges_;
}

Status TsSingleFileSegmenter::DoInitialize() {
  if (muxer_options_.output_file_name.empty())
    return Status(error::MUXER_FAILURE, "Single file output not specified.");

  output_file_.reset(File::Open(muxer_options_.output_file_name.c_str(), "w"));

  return Status::OK;
}

Status TsSingleFileSegmenter::DoFinalize() {
  if (!output_file_.release()->Close()) {
    return Status(
        error::FILE_FAILURE,
        "Cannot close file " + muxer_options_.output_file_name +
        ", possibly file permission issue or running out of disk space.");
  }

  return Status::OK;
}

Status TsSingleFileSegmenter::ProcessSegmentBuffer() {
  Range range;

  if(ranges_.empty()) {
    range.start = 0;
  } else {
    range.start = ranges_.back().end + 1;
  }

  range.end = range.start + segment_buffer_.Size() - 1; 
  ranges_.push_back(range);

  if (!output_file_) {
    return Status(error::FILE_FAILURE,
                  "Cannot open file for write " + muxer_options_.output_file_name);
  }

  RETURN_IF_ERROR(segment_buffer_.WriteToFile(output_file_.get()));

  return Status::OK;
}

std::string TsSingleFileSegmenter::GetSegmentPath() {
  return muxer_options_.output_file_name;
}

}  // namespace mp2t
}  // namespace media
}  // namespace shaka
