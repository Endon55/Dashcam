// Basic unit tests for utility helpers.
#include <gtest/gtest.h>
#include "Utils.h"

TEST(UtilsStrOrDefault, ReturnsInputWhenNonNull)
{
    const char* result = Utils::str_or_default("camera", "fallback");
    EXPECT_STREQ(result, "camera");
}

TEST(UtilsStrOrDefault, ReturnsFallbackWhenNull)
{
    const char* result = Utils::str_or_default(nullptr, "fallback");
    EXPECT_STREQ(result, "fallback");
}

TEST(UtilsStartsWith, PrefixMatch)
{
    EXPECT_TRUE(Utils::str_starts_with("dashcam", "dash"));
}

TEST(UtilsStartsWith, PrefixMismatch)
{
    EXPECT_FALSE(Utils::str_starts_with("dashcam", "cam"));
}

