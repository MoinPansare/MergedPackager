#ifndef PACKAGER_MEDIA_FORMATS_MP2T_ES_PARSER_SCTE35_H_
#define PACKAGER_MEDIA_FORMATS_MP2T_ES_PARSER_SCTE35_H_

#include <list>
#include <memory>
#include <utility>

#include "packager/base/callback.h"
#include "packager/base/compiler_specific.h"
#include "packager/media/base/byte_queue.h"
#include "packager/media/formats/mp2t/es_parser.h"
#include "packager/media/formats/mp2t/scte35_types.h"

namespace shaka {
namespace media {
class BitReader;

namespace mp2t {


class EsParserScte35: public EsParser {
 public:

  typedef base::Callback<void(uint32_t, const std::shared_ptr<splice_info_section_t>&)>
      NewSpliceInfoSectionCB;    

  EsParserScte35(uint32_t pid,
                const NewSpliceInfoSectionCB& new_splice_info_cb);
  ~EsParserScte35() override;

  // EsParser implementation.
  bool Parse(const uint8_t* buf, int size, int64_t pts, int64_t dts) override;
  void Flush() override;
  void Reset() override;

  

 private:
  EsParserScte35(const EsParserScte35&) = delete;
  EsParserScte35& operator=(const EsParserScte35&) = delete;


  // Discard some bytes from the ES stream.
  void DiscardEs(int nbytes);


  void PrintParsedSCTE35(std::shared_ptr<splice_info_section_t>);
  void PrintTimeSignal(const splice_time_t *);

  
  // Callbacks:
  NewSpliceInfoSectionCB new_splice_info_cb_;

  // Bytes of the ES stream that have not been emitted yet.
  //ByteQueue es_byte_queue_;

  // last pending parsed scte35 splice info section  
  std::shared_ptr<splice_info_section_t> sis_; 
  
};

}  // namespace mp2t
}  // namespace media
}  // namespace shaka

#endif  // PACKAGER_MEDIA_FORMATS_MP2T_ES_PARSER_SCTE35_H_
