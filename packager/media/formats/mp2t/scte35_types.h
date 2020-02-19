#ifndef SCTE35_TYPES_H_
#define SCTE35_TYPES_H_

/*
 scte35_types.h - Data structures for SCTE-35 splice_info_section and dependent types 
*/

#define SCTE35_START_EVENT(type) ((type == 0x30) || (type == 0x32) || (type == 0x34) || (type == 0x36))
#define SCTE35_END_EVENT(type) ((type == 0x31) || (type == 0x33) || (type == 0x35) || (type == 0x37))


typedef struct {
  uint8_t time_specified_flag;
  union {
    // if time_specified_flag == 1
    struct {
      uint8_t time_specified_flag_reserved;
      uint64_t pts_time;
    } time_spec;
    // else
    uint8_t reserved;
  };
} splice_time_t;

typedef struct {
  uint8_t auto_return;
  uint8_t reserved;
  uint64_t duration;
} break_duration_t;

typedef struct {
  uint32_t splice_event_id;
  uint8_t splice_event_cancel_indicator;
  uint8_t reserved;
  uint8_t out_of_network_indicator;
  uint8_t program_splice_flag;
  uint8_t duration_flag;
  uint8_t splice_immediate_flag;
  uint8_t splice_event_reserved;
  // if program_splice_flag == 1 && splice_immediate_flag == 0
  splice_time_t splice_time;
  uint8_t component_count;
  struct {
    uint8_t component_tag;
    splice_time_t splice_time;
  } component_t[255]; // up to component count
  // if duration_flag == 1
  break_duration_t break_duration;
  uint16_t unique_program_id;
  uint8_t avail_num;
  uint8_t avails_expected;
} splice_insert_t;

// base splice descriptor struct that every descriptor shares
typedef struct {
  uint8_t  splice_descriptor_tag;
  uint8_t  descriptor_length;
  uint32_t identifier;
} splice_descriptor_t;

typedef struct {
  uint8_t component_tag;
  uint8_t reserved;
  uint64_t pts_offset;
} component_tag_t;

typedef struct {
  uint8_t type;
  uint8_t length;
  long bit_start_index;
} segmentation_upid_t;

typedef struct {
  splice_descriptor_t descriptor;
  uint32_t segmentation_event_id;
  uint8_t segmentation_event_cancel_indicator;
  uint8_t reserved;
  // if segmentation_event_cancel_indicator == 0
  uint8_t program_segmentation_flag;
  uint8_t segmentation_duration_flag;
  uint8_t delivery_not_restricted_flag;
  union {
    // if delivery_not_restricted_flag == 0
    struct {
      uint8_t web_delivery_allowed_flag;
      uint8_t no_regional_blackout_flag;
      uint8_t archive_allowed_flag;
      uint8_t device_restrictions;
    } delivery_flags;
    // else
    uint8_t reserved_flags;
  };
  // if program_segmentation_flag == 0
  uint8_t component_count;
  component_tag_t component_tags[255]; // up to component_count
  // if segmentation_duration_flag == 1
  uint64_t segmentation_duration;
  uint8_t segmentation_upid_type;
  uint8_t segmentation_upid_length;
  uint8_t segmentation_upid_data[256];
  //void *segmentation_upid_ptr;  // up to segmentation_upid_length
  //void(*segmentation_upid_printer)(void *);
  uint8_t segmentation_type_id;
  uint8_t segment_num;
  uint8_t segments_expected;
  // if segmentation_type_id == 0x34 || 0x36
  uint8_t sub_segment_num;
  uint8_t sub_segments_expected;
} segmentation_descriptor_t;

typedef struct {
  uint8_t table_id;
  bool section_syntax_indicator;
  bool private_indicator;
  uint8_t reserved;
  uint16_t section_length;
  uint8_t  protocol_version;
  bool  encrypted_packet;
  uint8_t  encryption_algorithm;
  uint64_t pts_adjustment;
  uint8_t  cw_index;
  uint16_t tier;
  uint16_t splice_command_length;
  uint8_t  splice_command_type;
  union {
    splice_time_t splice_time_signal;
    splice_insert_t splice_insert;
  } splice_command;
  uint16_t descriptor_loop_length;
  // parsed only segmentation_descriptor for assignment
  //number of allocated segmentation_descriptor;
  uint16_t segmentation_descriptor_count;
  segmentation_descriptor_t segmentation_descriptor[8]; // will be descriptor_loop_length
  uint8_t alignment_stuffing_bytes_length;
  //uint8_t *alignment_stuffing; // size changes by using encryption algorithm
  int32_t e_crc_32;
  int32_t crc_32;
} splice_info_section_t;


#endif