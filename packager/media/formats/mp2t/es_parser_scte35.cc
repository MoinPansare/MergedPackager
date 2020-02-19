#include "packager/media/formats/mp2t/es_parser_scte35.h"

#include "packager/base/logging.h"
#include "packager/media/base/bit_reader.h"
#include "packager/media/formats/mp2t/mp2t_common.h"
#include "packager/media/formats/mp2t/ts_stream_type.h"
#include "packager/media/formats/mp2t/scte35_types.h"

namespace shaka {
namespace media {
namespace mp2t {


EsParserScte35::EsParserScte35(uint32_t pid,
                             const NewSpliceInfoSectionCB& new_splice_info_cb)
    : EsParser(pid),
      new_splice_info_cb_(new_splice_info_cb) {

	DVLOG(1) << "EsParserScte35 pid=" << pid;

}

EsParserScte35::~EsParserScte35() {}

bool EsParserScte35::Parse(const uint8_t* buf,
                          int size,
                          int64_t pts,
                          int64_t dts) {

  DVLOG(1) << __FUNCTION__;

  sis_ = std::make_shared<splice_info_section_t>();

  DCHECK(sis_);

  // Parse sis_->
  BitReader bit_reader(buf, size);

  // create parsed scte35 splice info 
  //splice_info_section_t section;

  RCHECK(bit_reader.ReadBits(8, &sis_->table_id));
  RCHECK(bit_reader.ReadBits(1, &sis_->section_syntax_indicator));
  RCHECK(bit_reader.ReadBits(1, &sis_->private_indicator));
  RCHECK(bit_reader.ReadBits(2, &sis_->reserved));
  RCHECK(bit_reader.ReadBits(12,&sis_->section_length));
  RCHECK(bit_reader.ReadBits(8, &sis_->protocol_version));
  RCHECK(bit_reader.ReadBits(1, &sis_->encrypted_packet));
  RCHECK(bit_reader.ReadBits(6, &sis_->encryption_algorithm));
  RCHECK(bit_reader.ReadBits(33,&sis_->pts_adjustment));
  RCHECK(bit_reader.ReadBits(8, &sis_->cw_index));
  RCHECK(bit_reader.ReadBits(12,&sis_->tier));
  RCHECK(bit_reader.ReadBits(12,&sis_->splice_command_length));
  RCHECK(bit_reader.ReadBits(8, &sis_->splice_command_type));



  switch (sis_->splice_command_type)
  {
    case 5: // splice_insert();
    {
      // set working pointer
      splice_insert_t *splice_insert = &sis_->splice_command.splice_insert;
      // parse insert command
      RCHECK(bit_reader.ReadBits(32, &splice_insert->splice_event_id));
      RCHECK(bit_reader.ReadBits(1, &splice_insert->splice_event_cancel_indicator));
      RCHECK(bit_reader.ReadBits(7, &splice_insert->reserved));
      if (splice_insert->splice_event_cancel_indicator == 0)
      {
         RCHECK(bit_reader.ReadBits(1, &splice_insert->out_of_network_indicator));
         RCHECK(bit_reader.ReadBits(1, &splice_insert->program_splice_flag));
         RCHECK(bit_reader.ReadBits(1, &splice_insert->duration_flag));
         RCHECK(bit_reader.ReadBits(1, &splice_insert->splice_immediate_flag));
         RCHECK(bit_reader.ReadBits(4, &splice_insert->splice_event_reserved));
        if (splice_insert->program_splice_flag == 1 &&
            splice_insert->splice_immediate_flag == 0)
        {
          RCHECK(bit_reader.ReadBits(1, &splice_insert->splice_time.time_specified_flag));
          if (splice_insert->splice_time.time_specified_flag)
          {
             RCHECK(bit_reader.ReadBits(6, &splice_insert->splice_time.time_spec.time_specified_flag_reserved));
             RCHECK(bit_reader.ReadBits(33, &splice_insert->splice_time.time_spec.pts_time));
          }
          else
          {
             RCHECK(bit_reader.ReadBits(7, &splice_insert->splice_time.reserved));
          }
        } 

        if (splice_insert->program_splice_flag == 0)
        {
          RCHECK(bit_reader.ReadBits(8, &splice_insert->component_count));
          for (int i = 0; i < splice_insert->component_count; i++)
          {
            RCHECK(bit_reader.ReadBits(8, &splice_insert->component_t[i].component_tag));
            if (splice_insert->splice_immediate_flag == 0)
            {
              RCHECK(bit_reader.ReadBits(1, &splice_insert->component_t[i].splice_time.time_specified_flag));
              if (splice_insert->component_t[i].splice_time.time_specified_flag)
              {
                RCHECK(bit_reader.ReadBits(6, &splice_insert->component_t[i].splice_time.time_spec.time_specified_flag_reserved));
                RCHECK(bit_reader.ReadBits(33, &splice_insert->component_t[i].splice_time.time_spec.pts_time));
              }
              else
              {
                RCHECK(bit_reader.ReadBits(7, &splice_insert->component_t[i].splice_time.reserved));
              }
            }
          }
        } // if splice_insert->program_splice_flag == 0

        if (splice_insert->duration_flag == 1)
        {
          RCHECK(bit_reader.ReadBits(1, &splice_insert->break_duration.auto_return));
          RCHECK(bit_reader.ReadBits(6, &splice_insert->break_duration.reserved));
          RCHECK(bit_reader.ReadBits(33, &splice_insert->break_duration.duration));
        } // if splice_insert->duration_flag == 1
        RCHECK(bit_reader.ReadBits(16, &splice_insert->unique_program_id));
        RCHECK(bit_reader.ReadBits(8, &splice_insert->avail_num));
        RCHECK(bit_reader.ReadBits(8, &splice_insert->avails_expected));
      } // if splice_event_cancel_indicator == 0
    }
    break;
    
    case 6: // time_signal();
    {
      // set working pointer
      splice_time_t *splice_time = &sis_->splice_command.splice_time_signal;

      RCHECK(bit_reader.ReadBits(1, &splice_time->time_specified_flag));
      if (splice_time->time_specified_flag)
      {
         RCHECK(bit_reader.ReadBits(6, &splice_time->time_spec.time_specified_flag_reserved));
         RCHECK(bit_reader.ReadBits(33, &splice_time->time_spec.pts_time));
      }
      else
      {
         RCHECK(bit_reader.ReadBits(7, &splice_time->reserved));
      }
    }
    break;
    
    default:
      LOG(ERROR) << "Unsupported splice command type! this application only accepts splice_insert and time_signal..";
      return false;
  } // switch

  // Parse descriptors 



  // indicates number of bytes following loop
  RCHECK(bit_reader.ReadBits(16, &sis_->descriptor_loop_length));


  // Allowing for a fixed number of descriptors so checking to ensure there is enough memory for it
  RCHECK(sis_->descriptor_loop_length <= sizeof(segmentation_descriptor_t)*8);


  // after this line, sum of all read bytes should be equal to descriptor_loop_length
  //int descriptor_loop_start_bits = filebuffer_bits_index;
  sis_->segmentation_descriptor_count = 0;
  int loop_remaining = sis_->descriptor_loop_length;

  while (loop_remaining > 0)
  {
    splice_descriptor_t splice_descriptor;

    DVLOG(1) << __FUNCTION__ << " loop_remaining=" << loop_remaining;

    RCHECK(bit_reader.ReadBits(8, &splice_descriptor.splice_descriptor_tag));
    RCHECK(bit_reader.ReadBits(8, &splice_descriptor.descriptor_length));
    RCHECK(bit_reader.ReadBits(32, &splice_descriptor.identifier));
     
    DVLOG(1) << "tag=" << (int)splice_descriptor.splice_descriptor_tag << ",length=" << (int)splice_descriptor.descriptor_length 
             << ",identifier=0x" << std::hex << splice_descriptor.identifier;
     

    if (splice_descriptor.splice_descriptor_tag != 0x02) // segmentation_descriptor
    {
              
      LOG(WARNING) << "This application only parses segmentation descriptors! Unsupported splice descriptor tag.." << (int)splice_descriptor.splice_descriptor_tag;

      // skip over this unsupported descriptor. already read the identifier so do not include that in the number of bytes
      RCHECK(bit_reader.SkipBytes(splice_descriptor.descriptor_length-4));
      // decrease length remaining by the length of this descriptor + the tag and length byte
      loop_remaining -= (splice_descriptor.descriptor_length+2);     
    }
    else {

      // parse the segmentation descriptor
      // set working ptr 
      segmentation_descriptor_t *psd = &sis_->segmentation_descriptor[sis_->segmentation_descriptor_count];
        
      psd->descriptor = splice_descriptor;
      RCHECK(bit_reader.ReadBits(32, &psd->segmentation_event_id));
      RCHECK(bit_reader.ReadBits(1, &psd->segmentation_event_cancel_indicator));
      RCHECK(bit_reader.ReadBits(7, &psd->reserved));
      if (psd->segmentation_event_cancel_indicator == 0)
      {
        RCHECK(bit_reader.ReadBits(1, &psd->program_segmentation_flag));
        RCHECK(bit_reader.ReadBits(1, &psd->segmentation_duration_flag));
        RCHECK(bit_reader.ReadBits(1, &psd->delivery_not_restricted_flag));
        if (psd->delivery_not_restricted_flag == 0)
        {
          RCHECK(bit_reader.ReadBits(1, &psd->delivery_flags.web_delivery_allowed_flag));
          RCHECK(bit_reader.ReadBits(1, &psd->delivery_flags.no_regional_blackout_flag));
          RCHECK(bit_reader.ReadBits(1, &psd->delivery_flags.archive_allowed_flag));
          RCHECK(bit_reader.ReadBits(2, &psd->delivery_flags.device_restrictions));
        }
        else
        {
          RCHECK(bit_reader.ReadBits(5, &psd->reserved_flags));
        }


        if (psd->program_segmentation_flag == 0)
        {
          RCHECK(bit_reader.ReadBits(8, &psd->component_count));
          for (int i = 0; i < psd->component_count; i++)
          {
            RCHECK(bit_reader.ReadBits(8, &psd->component_tags[sis_->segmentation_descriptor_count].component_tag));
            RCHECK(bit_reader.ReadBits(7, &psd->component_tags[sis_->segmentation_descriptor_count].reserved));
            RCHECK(bit_reader.ReadBits(33, &psd->component_tags[sis_->segmentation_descriptor_count].pts_offset));
          }
        }

        if (psd->segmentation_duration_flag == 1)
        {
          RCHECK(bit_reader.ReadBits(40, &psd->segmentation_duration));
        }

        RCHECK(bit_reader.ReadBits(8, &psd->segmentation_upid_type));
        RCHECK(bit_reader.ReadBits(8, &psd->segmentation_upid_length));

        for (int ii = 0; ii < psd->segmentation_upid_length; ii++) {
          RCHECK(bit_reader.ReadBits(8, &psd->segmentation_upid_data[ii]));
        }
        
        RCHECK(bit_reader.ReadBits(8, &psd->segmentation_type_id));
        RCHECK(bit_reader.ReadBits(8, &psd->segment_num));
        RCHECK(bit_reader.ReadBits(8, &psd->segments_expected));

        DVLOG(1) << __FUNCTION__ << " segmentation_type_id=0x" << std::hex << (int)psd->segmentation_type_id; 

        if (psd->segmentation_type_id == 0x34 || 
            psd->segmentation_type_id == 0x36)
        {
          //RCHECK(bit_reader.ReadBits(8, &psd->sub_segment_num));
          //RCHECK(bit_reader.ReadBits(8, &psd->sub_segments_expected));
        }
      } // if segmentation_event_cancel_indicator == 0

      // decrease bytes remaining by the length of the splice descriptor + the tag and length bytes
      loop_remaining -= (splice_descriptor.descriptor_length+2); 
      ++sis_->segmentation_descriptor_count;
    }
  } // while loop_remaining

  RCHECK(loop_remaining==0);
  
  if (sis_->encrypted_packet)
  {
    long total_read_bytes = bit_reader.bits_available() / 8;
    int remaining_section_bytes = sis_->section_length + 3 - total_read_bytes;

    sis_->alignment_stuffing_bytes_length = remaining_section_bytes - 4 - 4; // 4 byte e_crc, 4 byte crc
    
    RCHECK(bit_reader.SkipBytes(sis_->alignment_stuffing_bytes_length));
    RCHECK(bit_reader.ReadBits(32, &sis_->e_crc_32));
  }
  RCHECK(bit_reader.ReadBits(32, &sis_->crc_32));

  if (VLOG_IS_ON(1))
    PrintParsedSCTE35(sis_);

  // Emit the SCTE-35 splice info_section to the base parser
  new_splice_info_cb_.Run(pid(),sis_);    

  return true;
}

void EsParserScte35::Flush() {

if (sis_) {
    sis_ = std::shared_ptr<splice_info_section_t>();
  }
}

void EsParserScte35::Reset() {

    sis_ = std::shared_ptr<splice_info_section_t>();
}

  
void EsParserScte35::PrintParsedSCTE35(std::shared_ptr<splice_info_section_t> splice_info) {
  
  printf("splice_info_section() {\n");
    
  printf("  table_id : %d\n", splice_info->table_id);
  printf("  section_syntax_indicator : %d\n", splice_info->section_syntax_indicator);
  printf("  private_indicator : %d\n", splice_info->private_indicator);
  printf("  reserved : %d\n", splice_info->reserved);
  printf("  section_length : %d\n", splice_info->section_length);
  printf("  protocol_version : %d\n", splice_info->protocol_version);
  printf("  encrypted_packet : %d\n", splice_info->encrypted_packet);
  printf("  encryption_algorithm : %d\n", splice_info->encryption_algorithm);
#if defined(OS_WIN)  
  printf("  pts_adjustment : %I64u\n", splice_info->pts_adjustment);
#else  
  printf("  pts_adjustment : %lu\n", splice_info->pts_adjustment);
#endif  
  printf("  cw_index : %d\n", splice_info->cw_index);
  printf("  tier : %d\n", splice_info->tier);
  printf("  splice_command_length : %d\n", splice_info->splice_command_length);
  printf("  splice_command_type : %d\n", splice_info->splice_command_type);

  // dump splice_command
  switch (splice_info->splice_command_type) {

    case 5:
    {
        printf("  splice_insert() {\n");
        printf("  *** NOT IMPLEMENTED ***\n");
        printf("  }\n");

    }
    break;


    case 6: 
    {

        PrintTimeSignal(&splice_info->splice_command.splice_time_signal);
    }
    break;


    default:
    {

    }
  } // switch

  // print descriptor section

  printf("  descriptor_loop_length : %d\n", splice_info->descriptor_loop_length);
  printf("  splice_descriptor() {\n");

//#if defined (PRINT_DESCRIPTORS)          

  for (int i = 0; i < splice_info->segmentation_descriptor_count; ++i) {

      printf("    segmentation_descriptor() {\n");
      
      printf("      splice_descriptor_tag : %d\n", splice_info->segmentation_descriptor[i].descriptor.splice_descriptor_tag);
      printf("      descriptor_length : %d\n", splice_info->segmentation_descriptor[i].descriptor.descriptor_length);
      printf("      identifier : 0x%x\n", splice_info->segmentation_descriptor[i].descriptor.identifier);
      printf("      segmentation_event_id : %d\n", splice_info->segmentation_descriptor[i].segmentation_event_id);
      printf("      segmentation_event_cancel_indicator : %d\n", splice_info->segmentation_descriptor[i].segmentation_event_cancel_indicator);
      printf("      reserved : %d\n", splice_info->segmentation_descriptor[i].reserved);
      
      if (splice_info->segmentation_descriptor[i].segmentation_event_cancel_indicator == 0) {
    
        printf("      program_segmentation_flag : %d\n", splice_info->segmentation_descriptor[i].program_segmentation_flag);
        printf("      segmentation_duration_flag : %d\n", splice_info->segmentation_descriptor[i].segmentation_duration_flag);
        printf("      delivery_not_restricted_flag : %d\n", splice_info->segmentation_descriptor[i].delivery_not_restricted_flag);
        
        if (splice_info->segmentation_descriptor[i].delivery_not_restricted_flag == 0) {
        
          printf("      web_delivery_allowed_flag : %d\n", splice_info->segmentation_descriptor[i].delivery_flags.web_delivery_allowed_flag);
          printf("      no_regional_blackout_flag : %d\n", splice_info->segmentation_descriptor[i].delivery_flags.no_regional_blackout_flag);
          printf("      archive_allowed_flag : %d\n", splice_info->segmentation_descriptor[i].delivery_flags.archive_allowed_flag);
          printf("      device_restrictions : %d\n", splice_info->segmentation_descriptor[i].delivery_flags.device_restrictions);
        }
        else {
        
          printf("      reserved_flags : %d\n", splice_info->segmentation_descriptor[i].reserved_flags);
        }
        
        if (splice_info->segmentation_descriptor[i].program_segmentation_flag == 0) {
        
          printf("      component_count : %d\n", splice_info->segmentation_descriptor[i].component_count);
          
          for (int j = 0; j < splice_info->segmentation_descriptor[i].component_count; j++) {
          
            printf("      {\n");
            printf("        component_tag : %d\n", splice_info->segmentation_descriptor[i].component_tags[j].component_tag);
            printf("        reserved : %d\n", splice_info->segmentation_descriptor[i].component_tags[j].reserved);
#if defined(OS_WIN)  
            printf("        pts_offset : %I64u\n", splice_info->segmentation_descriptor[i].component_tags[j].pts_offset);
#else
            printf("        pts_offset : %lu\n", splice_info->segmentation_descriptor[i].component_tags[j].pts_offset);
#endif            
            printf("      }\n");
          }
        } // program_segmentation_flag == 0

        if (splice_info->segmentation_descriptor[i].segmentation_duration_flag == 1) {
        
#if defined(OS_WIN)            
          printf("      segmentation_duration : %I64u\n", splice_info->segmentation_descriptor[i].segmentation_duration);
#else
          printf("      segmentation_duration : %lu\n", splice_info->segmentation_descriptor[i].segmentation_duration);
#endif          
        } // segmentation_duration_flag == 1
        printf("      segmentation_upid_type : %d\n", splice_info->segmentation_descriptor[i].segmentation_upid_type);
        printf("      segmentation_upid_length : %d\n", splice_info->segmentation_descriptor[i].segmentation_upid_length);

        // TODO: print segmentation_upid by its type!!



        if (splice_info->segmentation_descriptor[i].segmentation_upid_type == 0x09) {

          std::string s = std::string((char *)splice_info->segmentation_descriptor[i].segmentation_upid_data,
                                      splice_info->segmentation_descriptor[i].segmentation_upid_length);  
          printf("      segmentation_upid : %s\n", s.c_str());
        }
        else
        for (int k = 0; k < splice_info->segmentation_descriptor[i].segmentation_upid_length; k++) {
        
          printf("      segmentation_upid[%d] : 0x%02X\n", k, splice_info->segmentation_descriptor[i].segmentation_upid_data[k]);
        }
        
        printf("      segmentation_type_id : 0x%02X\n", splice_info->segmentation_descriptor[i].segmentation_type_id);
        printf("      segment_num : %d\n", splice_info->segmentation_descriptor[i].segment_num);
        printf("      segments_expected : %d\n", splice_info->segmentation_descriptor[i].segments_expected);
        
        if (splice_info->segmentation_descriptor[i].segmentation_type_id == 0x34 || 
            splice_info->segmentation_descriptor[i].segmentation_type_id == 0x36) {
        
          printf("      sub_segment_num : %d\n", splice_info->segmentation_descriptor[i].sub_segment_num);
          printf("      sub_segments_expected : %d\n", splice_info->segmentation_descriptor[i].sub_segments_expected);
        }
      } // if segmentation_event_cancel_indicator == 0

      printf("    }\n"); // segmentation_descriptor()
  } // for

  printf("  }\n"); // splice_descriptor() 
 

  printf("}\n"); // splice_info_section() 
   
}

void EsParserScte35::PrintTimeSignal(const splice_time_t *ptr) {

  printf("  time_signal() {\n");
  printf("    splice_time() {\n");
  printf("      time_specified_flag : %d\n", ptr->time_specified_flag);
  if (ptr->time_specified_flag == 1)
  {
    printf("      reserved : %d\n", ptr->time_spec.time_specified_flag_reserved);
#if defined(OS_WIN)  
    printf("      pts_time : %I64d\n", ptr->time_spec.pts_time);
#else    
    printf("      pts_time : %lu\n", ptr->time_spec.pts_time);
#endif    
  }
  else
  {
    printf("      reserved : %d\n", ptr->reserved);
  }
  printf("    }\n"); // splice_time()
  printf("  }\n"); // time_signal
}


}  // namespace mp2t
}  // namespace media
}  // namespace shaka
