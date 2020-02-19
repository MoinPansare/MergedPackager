// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PACKAGER_MEDIA_FORMATS_MP2T_TS_SECTION_DATA_H_
#define PACKAGER_MEDIA_FORMATS_MP2T_TS_SECTION_DATA_H_

#include <stdint.h>
#include <memory>
#include "packager/base/compiler_specific.h"
#include "packager/media/base/byte_queue.h"
#include "packager/media/formats/mp2t/ts_section.h"

namespace shaka {
namespace media {
namespace mp2t {

class EsParser;

class TsSectionData : public TsSection {
 public:
  explicit TsSectionData(std::unique_ptr<EsParser> es_parser);
  ~TsSectionData() override;

  // TsSection implementation.
  bool Parse(bool payload_unit_start_indicator,
             const uint8_t* buf,
             int size) override;
  void Flush() override;
  void Reset() override;

 private:
  
  // Bytes of the current data section
  ByteQueue data_byte_queue_;

  // ES parser.
  std::unique_ptr<EsParser> es_parser_;

  // Do not start parsing before getting a unit start indicator.
  bool wait_for_pusi_;

  
  DISALLOW_COPY_AND_ASSIGN(TsSectionData);
};

}  // namespace mp2t
}  // namespace media
}  // namespace shaka

#endif

