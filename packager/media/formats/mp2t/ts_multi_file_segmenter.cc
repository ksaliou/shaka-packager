// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/media/formats/mp2t/ts_multi_file_segmenter.h"

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

TsMultiFileSegmenter::TsMultiFileSegmenter(const MuxerOptions& options, MuxerListener* listener)
    : TsSegmenter(options, listener) {}

TsMultiFileSegmenter::~TsMultiFileSegmenter() {}

bool TsMultiFileSegmenter::GetInitRange(size_t* offset, size_t* size) {
  VLOG(1) << "MultiSegmentSegmenter outputs init segment: "
          << muxer_options_.segment_template;
  return false;
}

bool TsMultiFileSegmenter::GetIndexRange(size_t* offset, size_t* size) {
  VLOG(1) << "MultiSegmentSegmenter does not have index range.";
  return false;
}

std::vector<Range> TsMultiFileSegmenter::GetSegmentRanges() {
  VLOG(1) << "MultiSegmentSegmenter does not have media segment ranges.";
  return std::vector<Range>();
}

Status TsMultiFileSegmenter::DoInitialize() {
  if (muxer_options_.segment_template.empty())
    return Status(error::MUXER_FAILURE, "Segment template not specified.");

  return Status::OK;
}

Status TsMultiFileSegmenter::DoFinalize() {
  return Status::OK;
}

Status TsMultiFileSegmenter::ProcessSegmentBuffer() {
  std::string segment_path =
      GetSegmentName(muxer_options_.segment_template, segment_start_timestamp_,
                      segment_number_, muxer_options_.bandwidth);

  std::unique_ptr<File, FileCloser> segment_file;
  segment_file.reset(File::Open(segment_path.c_str(), "w"));
  if (!segment_file) {
    return Status(error::FILE_FAILURE,
                  "Cannot open file for write " + segment_path);
  }

  RETURN_IF_ERROR(segment_buffer_.WriteToFile(segment_file.get()));

  if (!segment_file.release()->Close()) {
    return Status(
        error::FILE_FAILURE,
        "Cannot close file " + segment_path +
        ", possibly file permission issue or running out of disk space.");
  }

  return Status::OK;
}

std::string TsMultiFileSegmenter::GetSegmentPath() {
  return GetSegmentName(muxer_options_.segment_template, segment_start_timestamp_,
                      segment_number_, muxer_options_.bandwidth);
}

}  // namespace mp2t
}  // namespace media
}  // namespace shaka
