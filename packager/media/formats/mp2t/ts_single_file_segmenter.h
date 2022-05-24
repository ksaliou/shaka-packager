// Copyright 2016 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#ifndef PACKAGER_MEDIA_FORMATS_MP2T_TS_SINGLE_FILE_SEGMENTER_H_
#define PACKAGER_MEDIA_FORMATS_MP2T_TS_SINGLE_FILE_SEGMENTER_H_

#include <memory>
#include "packager/file/file.h"
#include "packager/media/base/muxer_options.h"
#include "packager/media/formats/mp2t/pes_packet_generator.h"
#include "packager/media/formats/mp2t/ts_writer.h"
#include "packager/media/formats/mp2t/ts_segmenter.h"
#include "packager/status.h"

namespace shaka {
namespace media {

class KeySource;
class MuxerListener;

namespace mp2t {

// TODO(rkuroiwa): For now, this implements multifile segmenter. Like other
// make this an abstract super class and implement multifile and single file
// segmenters.
class TsSingleFileSegmenter : public TsSegmenter {
 public:
  // TODO(rkuroiwa): Add progress listener?
  /// @param options is the options for this muxer. This must stay valid
  ///        throughout the life time of the instance.
  /// @param listener is the MuxerListener that should be used to notify events.
  ///        This may be null, in which case no events are sent.
  TsSingleFileSegmenter(const MuxerOptions& options, MuxerListener* listener);
  ~TsSingleFileSegmenter();

   /// @name Segmenter implementation overrides.
  /// @{
  bool GetInitRange(size_t* offset, size_t* size) override;
  bool GetIndexRange(size_t* offset, size_t* size) override;
  std::vector<Range> GetSegmentRanges() override;
  /// @}
 private:
  std::vector<Range> ranges_;
  std::unique_ptr<File, FileCloser> output_file_;
  Status DoInitialize() override;
  Status DoFinalize() override;
  Status ProcessSegmentBuffer() override;
  std::string GetSegmentPath() override;

  DISALLOW_COPY_AND_ASSIGN(TsSingleFileSegmenter);
};

}  // namespace mp2t
}  // namespace media
}  // namespace shaka
#endif  // PACKAGER_MEDIA_FORMATS_MP2T_TS_SINGLE_FILE_SEGMENTER_H_
