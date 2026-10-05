#include <gtest/gtest.h>

#include "network/OtaVersion.h"

using OtaVersion::compare;
using OtaVersion::isFirmwareAssetFor;
using OtaVersion::parse;

TEST(OtaVersion, ParsesTagsAndProductVersions) {
  const auto tag = parse("v0.1.0");
  EXPECT_TRUE(tag.valid);
  EXPECT_EQ(tag.segments[0], 0);
  EXPECT_EQ(tag.segments[1], 1);
  EXPECT_EQ(tag.segments[2], 0);
  EXPECT_FALSE(tag.prerelease);

  const auto beta = parse("0.1.0-beta");
  EXPECT_TRUE(beta.valid);
  EXPECT_TRUE(beta.prerelease);

  EXPECT_TRUE(parse("1.6.1-rc").prerelease);
  EXPECT_TRUE(parse("0.1.0-dev+0a602499").prerelease);
  EXPECT_TRUE(parse("2.0.0-Alpha1").prerelease);
  EXPECT_FALSE(parse("1.6.0-x4-pro").prerelease);  // CrossInk's device suffix is not a prerelease
  EXPECT_FALSE(parse("dev").valid);
  EXPECT_FALSE(parse("").valid);
  EXPECT_FALSE(parse(nullptr).valid);
}

TEST(OtaVersion, ReleaseIsNewerThanItsBeta) {
  EXPECT_GT(compare("v0.1.0", "0.1.0-beta"), 0);
  EXPECT_LT(compare("v0.1.0-beta", "0.1.0"), 0);
  EXPECT_EQ(compare("v0.1.0-beta", "0.1.0-beta"), 0);
  EXPECT_EQ(compare("v0.1.0-beta2", "0.1.0-beta"), 0);  // same number, both prereleases: tie
}

TEST(OtaVersion, NumbersDecideFirst) {
  EXPECT_GT(compare("v0.1.1", "0.1.0"), 0);
  EXPECT_GT(compare("v0.2.0-beta", "0.1.0"), 0);  // a newer prerelease still beats an older release
  EXPECT_LT(compare("v0.1.0", "0.1.1-beta"), 0);
  EXPECT_GT(compare("v1.0.0", "0.9.9"), 0);
  EXPECT_EQ(compare("v0.1.0", "0.1.0"), 0);
  EXPECT_EQ(compare("garbage", "0.1.0"), 0);
}

TEST(OtaVersion, MatchesOnlyThisDevicesImage) {
  EXPECT_TRUE(isFirmwareAssetFor("crossindix-0.1.0-beta-x4-pro.bin", "x4-pro"));
  EXPECT_TRUE(isFirmwareAssetFor("crossindix-0.1.0-x3-x4.bin", "x3-x4"));
  EXPECT_TRUE(isFirmwareAssetFor("crossindix-1.2.3-x4-classic.bin", "x4-classic"));

  EXPECT_FALSE(isFirmwareAssetFor("crossindix-0.1.0-beta-x4-pro.bin", "x3-x4"));
  EXPECT_FALSE(isFirmwareAssetFor("crossindix-0.1.0-beta-x4-classic.bin", "x4-pro"));
  EXPECT_FALSE(isFirmwareAssetFor("crossindix-0.1.0-x4.bin", "x3-x4"));
  EXPECT_FALSE(isFirmwareAssetFor("firmware-x4-pro-v1.6.0.bin", "x4-pro"));  // CrossInk's own naming
  EXPECT_FALSE(isFirmwareAssetFor("crossindix-x4-pro.bin", "x4-pro"));       // no version
  EXPECT_FALSE(isFirmwareAssetFor("SHA256SUMS.txt", "x4-pro"));
  EXPECT_FALSE(isFirmwareAssetFor("crossindix-0.1.0-x4-pro.bin.sha256", "x4-pro"));
  EXPECT_FALSE(isFirmwareAssetFor(nullptr, "x4-pro"));
  EXPECT_FALSE(isFirmwareAssetFor("crossindix-0.1.0-x4-pro.bin", ""));
}
