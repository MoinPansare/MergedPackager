// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "packager/media/formats/mp2t/ts_section_data.h"

#include "packager/base/logging.h"
#include "packager/base/strings/string_number_conversions.h"
#include "packager/media/base/bit_reader.h"
#include "packager/media/base/timestamp.h"
#include "packager/media/formats/mp2t/es_parser.h"
#include "packager/media/formats/mp2t/mp2t_common.h"


namespace shaka {
namespace media {
namespace mp2t {

TsSectionData::TsSectionData(std::unique_ptr<EsParser> es_parser)   
:   es_parser_(es_parser.release()),
    wait_for_pusi_(true) {
    DCHECK(es_parser_);
}

TsSectionData::~TsSectionData() {
}

bool TsSectionData::Parse(bool payload_unit_start_indicator,
                         const uint8_t* buf,
                         int size) {

  // Ignore partial data payloads
  if (wait_for_pusi_ && !payload_unit_start_indicator)
    return true;

  if (payload_unit_start_indicator) {
    
    // Reset the state of the Data section.
    Reset();
    
    DCHECK_GE(size, 1);

    uint8_t pointer = *buf;

    // skip over pointer field and move to start of section payload
    buf++;
    size--;
    buf = buf + pointer; 
    size -= pointer;
  }
    
  return es_parser_->Parse(buf, size, kNoTimestamp, kNoTimestamp);
}

void TsSectionData::Flush() {
  
  // Flush the underlying ES parser.
  es_parser_->Flush();
}

void TsSectionData::Reset() {
  
  es_parser_->Reset();
}


}  // namespace mp2t
}  // namespace media
}  // namespace shaka
