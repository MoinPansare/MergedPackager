// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <gflags/gflags.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <libxml/tree.h>

#include <list>

#include "packager/base/logging.h"
#include "packager/base/strings/string_util.h"
#include "packager/mpd/base/segment_info.h"
#include "packager/mpd/base/xml/xml_node.h"
#include "packager/mpd/test/xml_compare.h"

DECLARE_bool(segment_template_constant_duration);

using ::testing::ElementsAre;

namespace shaka {
namespace xml {

namespace {

// Template so that it works for ContentProtectionXml and
// ContentProtectionXml::Element.
template <typename XmlElement>
void AddAttribute(const std::string& name,
                  const std::string& value,
                  XmlElement* content_protection_xml) {
  MediaInfo::ContentProtectionXml::AttributeNameValuePair* attribute =
      content_protection_xml->add_attributes();
  attribute->set_name(name);
  attribute->set_value(value);
}

}  // namespace

// Make sure XmlEqual() is functioning correctly.
// TODO(rkuroiwa): Move this to a separate file. This requires it to be TEST
// due to gtest /test
TEST(XmlNodeTest, MetaTestXmlElementsEqual) {
  static const char kXml1[] =
      "<A>\n"
      "  <B\n"
      "    c=\"1\""
      "    e=\"foobar\""
      "    somelongnameattribute=\"somevalue\">\n"
      "      <Bchild childvalue=\"3\"\n"
      "              f=\"4\"/>\n"
      "  </B>\n"
      "  <C />\n"
      "</A>";


  // This is same as kXml1 but the attributes are reordered. Note that the
  // children are not reordered.
  static const char kXml1AttributeReorder[] =
      "<A>\n"
      "  <B\n"
      "    c=\"1\""
      "    somelongnameattribute=\"somevalue\"\n"
      "    e=\"foobar\">"
      "      <Bchild childvalue=\"3\"\n"
      "              f=\"4\"/>\n"
      "  </B>\n"
      "  <C />\n"
      "</A>";

  // <C> is before <B>.
  static const char kXml1ChildrenReordered[] =
      "<A>\n"
      "  <C />\n"
      "  <B\n"
      "    d=\"2\""
      "    c=\"1\""
      "    somelongnameattribute=\"somevalue\"\n"
      "    e=\"foobar\">"
      "      <Bchild childvalue=\"3\"\n"
      "              f=\"4\"/>\n"
      "  </B>\n"
      "</A>";

  // <C> is before <B>.
  static const char kXml1RemovedAttributes[] =
      "<A>\n"
      "  <B\n"
      "    d=\"2\"\n>"
      "      <Bchild f=\"4\"/>\n"
      "  </B>\n"
      "  <C />\n"
      "</A>";

  static const char kXml2[] =
      "<A>\n"
      "  <C />\n"
      "</A>";

  // In XML <C />, <C></C>, and <C/> mean the same thing.
  static const char kXml2DifferentSyntax[] =
      "<A>\n"
      "  <C></C>\n"
      "</A>";

  static const char kXml2MoreDifferentSyntax[] =
      "<A>\n"
      "  <C/>\n"
      "</A>";

  // Identity.
  ASSERT_TRUE(XmlEqual(kXml1, kXml1));

  // Equivalent.
  ASSERT_TRUE(XmlEqual(kXml1, kXml1AttributeReorder));
  ASSERT_TRUE(XmlEqual(kXml2, kXml2DifferentSyntax));
  ASSERT_TRUE(XmlEqual(kXml2, kXml2MoreDifferentSyntax));

  // Different.
  ASSERT_FALSE(XmlEqual(kXml1, kXml2));
  ASSERT_FALSE(XmlEqual(kXml1, kXml1ChildrenReordered));
  ASSERT_FALSE(XmlEqual(kXml1, kXml1RemovedAttributes));
  ASSERT_FALSE(XmlEqual(kXml1AttributeReorder, kXml1ChildrenReordered));
}

// Verify that if contents are different, XmlEqual returns false.
// This is to catch the case where just using xmlNodeGetContent() on elements
// that have subelements don't quite work well.
// xmlNodeGetContent(<A>) (for both <A>s) will return "content1content2".
// But if it is run on <B> for the first XML, it will return "content1", but
// for second XML will return "c".
TEST(XmlNodeTest, MetaTestXmlEqualDifferentContent) {
  ASSERT_FALSE(XmlEqual(
      "<A><B>content1</B><B>content2</B></A>",
      "<A><B>c</B><B>ontent1content2</B></A>"));
}

TEST(XmlNodeTest, ExtractReferencedNamespaces) {
  XmlNode grand_child_with_namespace("grand_ns:grand_child");
  grand_child_with_namespace.SetContent("grand child content");

  XmlNode child("child1");
  child.SetContent("child1 content");
  child.AddChild(grand_child_with_namespace.PassScopedPtr());

  XmlNode child_with_namespace("child_ns:child2");
  child_with_namespace.SetContent("child2 content");

  XmlNode root("root");
  root.AddChild(child.PassScopedPtr());
  root.AddChild(child_with_namespace.PassScopedPtr());

  EXPECT_THAT(root.ExtractReferencedNamespaces(),
              ElementsAre("child_ns", "grand_ns"));
}

TEST(XmlNodeTest, ExtractReferencedNamespacesFromAttributes) {
  XmlNode child("child");
  child.SetStringAttribute("child_attribute_ns:attribute",
                           "child attribute value");

  XmlNode root("root");
  root.AddChild(child.PassScopedPtr());
  root.SetStringAttribute("root_attribute_ns:attribute",
                          "root attribute value");

  EXPECT_THAT(root.ExtractReferencedNamespaces(),
              ElementsAre("child_attribute_ns", "root_attribute_ns"));
}

// Verify that AddContentProtectionElements work.
// xmlReadMemory() (used in XmlEqual()) doesn't like XML fragments that have
// namespaces without context, e.g. <cenc:pssh> element.
// The MpdBuilderTests work because the MPD element has xmlns:cenc attribute.
// Tests that have <cenc:pssh> is in mpd_builder_unittest.
TEST(XmlNodeTest, AddContentProtectionElements) {
  std::list<ContentProtectionElement> content_protections;
  ContentProtectionElement content_protection_widevine;
  content_protection_widevine.scheme_id_uri =
      "urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed";
  content_protection_widevine.value = "SOME bogus Widevine DRM version";
  Element any_element;
  any_element.name = "AnyElement";
  any_element.content = "any content";
  content_protection_widevine.subelements.push_back(any_element);
  content_protections.push_back(content_protection_widevine);

  ContentProtectionElement content_protection_clearkey;
  content_protection_clearkey.scheme_id_uri =
      "urn:uuid:1077efec-c0b2-4d02-ace3-3c1e52e2fb4b";
  content_protections.push_back(content_protection_clearkey);

  RepresentationXmlNode representation;
  representation.AddContentProtectionElements(content_protections);
  EXPECT_THAT(
      representation.GetRawPtr(),
      XmlNodeEqual(
          "<Representation>\n"
          " <ContentProtection\n"
          "   schemeIdUri=\"urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed\"\n"
          "   value=\"SOME bogus Widevine DRM version\">\n"
          "     <AnyElement>any content</AnyElement>\n"
          " </ContentProtection>\n"
          " <ContentProtection\n"
          "   schemeIdUri=\"urn:uuid:1077efec-c0b2-4d02-ace3-3c1e52e2fb4b\">"
          " </ContentProtection>\n"
          "</Representation>"));
}

TEST(XmlNodeTest, AddEC3AudioInfo) {
  MediaInfo::AudioInfo audio_info;
  audio_info.set_codec("ec-3");
  audio_info.set_sampling_frequency(44100);
  audio_info.mutable_codec_specific_data()->set_ec3_channel_map(0xF801);

  RepresentationXmlNode representation;
  representation.AddAudioInfo(audio_info);
  EXPECT_THAT(
      representation.GetRawPtr(),
      XmlNodeEqual(
          "<Representation audioSamplingRate=\"44100\">\n"
          "  <AudioChannelConfiguration\n"
          "   schemeIdUri=\n"
          "    \"tag:dolby.com,2014:dash:audio_channel_configuration:2011\"\n"
          "   value=\"F801\"/>\n"
          "</Representation>\n"));
}

class LiveSegmentTimelineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    FLAGS_segment_template_constant_duration = true;
    media_info_.set_segment_template_url("$Number$.m4s");
  }

  void TearDown() override { FLAGS_segment_template_constant_duration = false; }

  MediaInfo media_info_;
};

TEST_F(LiveSegmentTimelineTest, OneSegmentInfo) {
  const uint32_t kStartNumber = 1;
  const uint64_t kStartTime = 0;
  const uint64_t kDuration = 100;
  const uint64_t kRepeat = 9;

  std::list<SegmentInfo> segment_infos = {
      {kStartTime, kDuration, kRepeat},
  };
  RepresentationXmlNode representation;
  ASSERT_TRUE(
      representation.AddLiveOnlyInfo(media_info_, segment_infos, kStartNumber));

  EXPECT_THAT(
      representation.GetRawPtr(),
      XmlNodeEqual("<Representation>"
                   "  <SegmentTemplate media=\"$Number$.m4s\" "
                   "                   startNumber=\"1\" duration=\"100\"/>"
                   "</Representation>"));
}

TEST_F(LiveSegmentTimelineTest, OneSegmentInfoNonZeroStartTime) {
  const uint32_t kStartNumber = 1;
  const uint64_t kNonZeroStartTime = 500;
  const uint64_t kDuration = 100;
  const uint64_t kRepeat = 9;

  std::list<SegmentInfo> segment_infos = {
      {kNonZeroStartTime, kDuration, kRepeat},
  };
  RepresentationXmlNode representation;
  ASSERT_TRUE(
      representation.AddLiveOnlyInfo(media_info_, segment_infos, kStartNumber));

  EXPECT_THAT(representation.GetRawPtr(),
              XmlNodeEqual(
                  "<Representation>"
                  "  <SegmentTemplate media=\"$Number$.m4s\" startNumber=\"1\">"
                  "    <SegmentTimeline>"
                  "      <S t=\"500\" d=\"100\" r=\"9\"/>"
                  "    </SegmentTimeline>"
                  "  </SegmentTemplate>"
                  "</Representation>"));
}

TEST_F(LiveSegmentTimelineTest, OneSegmentInfoMatchingStartTimeAndNumber) {
  const uint32_t kStartNumber = 6;
  const uint64_t kNonZeroStartTime = 500;
  const uint64_t kDuration = 100;
  const uint64_t kRepeat = 9;

  std::list<SegmentInfo> segment_infos = {
      {kNonZeroStartTime, kDuration, kRepeat},
  };
  RepresentationXmlNode representation;
  ASSERT_TRUE(
      representation.AddLiveOnlyInfo(media_info_, segment_infos, kStartNumber));

  EXPECT_THAT(
      representation.GetRawPtr(),
      XmlNodeEqual("<Representation>"
                   "  <SegmentTemplate media=\"$Number$.m4s\" "
                   "                   startNumber=\"6\" duration=\"100\"/>"
                   "</Representation>"));
}

TEST_F(LiveSegmentTimelineTest, AllSegmentsSameDurationExpectLastOne) {
  const uint32_t kStartNumber = 1;

  const uint64_t kStartTime1 = 0;
  const uint64_t kDuration1 = 100;
  const uint64_t kRepeat1 = 9;

  const uint64_t kStartTime2 = kStartTime1 + (kRepeat1 + 1) * kDuration1;
  const uint64_t kDuration2 = 200;
  const uint64_t kRepeat2 = 0;

  std::list<SegmentInfo> segment_infos = {
      {kStartTime1, kDuration1, kRepeat1},
      {kStartTime2, kDuration2, kRepeat2},
  };
  RepresentationXmlNode representation;
  ASSERT_TRUE(
      representation.AddLiveOnlyInfo(media_info_, segment_infos, kStartNumber));

  EXPECT_THAT(
      representation.GetRawPtr(),
      XmlNodeEqual("<Representation>"
                   "  <SegmentTemplate media=\"$Number$.m4s\" "
                   "                   startNumber=\"1\" duration=\"100\"/>"
                   "</Representation>"));
}

TEST_F(LiveSegmentTimelineTest, SecondSegmentInfoNonZeroRepeat) {
  const uint32_t kStartNumber = 1;

  const uint64_t kStartTime1 = 0;
  const uint64_t kDuration1 = 100;
  const uint64_t kRepeat1 = 9;

  const uint64_t kStartTime2 = kStartTime1 + (kRepeat1 + 1) * kDuration1;
  const uint64_t kDuration2 = 200;
  const uint64_t kRepeat2 = 1;

  std::list<SegmentInfo> segment_infos = {
      {kStartTime1, kDuration1, kRepeat1},
      {kStartTime2, kDuration2, kRepeat2},
  };
  RepresentationXmlNode representation;
  ASSERT_TRUE(
      representation.AddLiveOnlyInfo(media_info_, segment_infos, kStartNumber));

  EXPECT_THAT(representation.GetRawPtr(),
              XmlNodeEqual(
                  "<Representation>"
                  "  <SegmentTemplate media=\"$Number$.m4s\" startNumber=\"1\">"
                  "    <SegmentTimeline>"
                  "      <S t=\"0\" d=\"100\" r=\"9\"/>"
                  "      <S t=\"1000\" d=\"200\" r=\"1\"/>"
                  "    </SegmentTimeline>"
                  "  </SegmentTemplate>"
                  "</Representation>"));
}

TEST_F(LiveSegmentTimelineTest, TwoSegmentInfoWithGap) {
  const uint32_t kStartNumber = 1;

  const uint64_t kStartTime1 = 0;
  const uint64_t kDuration1 = 100;
  const uint64_t kRepeat1 = 9;

  const uint64_t kGap = 100;
  const uint64_t kStartTime2 = kGap + kStartTime1 + (kRepeat1 + 1) * kDuration1;
  const uint64_t kDuration2 = 200;
  const uint64_t kRepeat2 = 0;

  std::list<SegmentInfo> segment_infos = {
      {kStartTime1, kDuration1, kRepeat1},
      {kStartTime2, kDuration2, kRepeat2},
  };
  RepresentationXmlNode representation;
  ASSERT_TRUE(
      representation.AddLiveOnlyInfo(media_info_, segment_infos, kStartNumber));

  EXPECT_THAT(representation.GetRawPtr(),
              XmlNodeEqual(
                  "<Representation>"
                  "  <SegmentTemplate media=\"$Number$.m4s\" startNumber=\"1\">"
                  "    <SegmentTimeline>"
                  "      <S t=\"0\" d=\"100\" r=\"9\"/>"
                  "      <S t=\"1100\" d=\"200\"/>"
                  "    </SegmentTimeline>"
                  "  </SegmentTemplate>"
                  "</Representation>"));
}

}  // namespace xml
}  // namespace shaka
