/**
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include "runtime/include/thread_scopes.h"
#include "runtime/include/runtime.h"
#include "runtime/ets_vm_api.h"
#include <map>

namespace panda::ets::test {

// NOLINTBEGIN(google-runtime-int)
[[maybe_unused]] static long SkoalaCreateRedrawerPeer([[maybe_unused]] panda::Method *m,
                                                      [[maybe_unused]] void *object /*any*/)
{
    return 1;
}
[[maybe_unused]] static long SkoalaGetFrame([[maybe_unused]] panda::Method *m,
                                            [[maybe_unused]] long peer /*KNativePointer*/, [[maybe_unused]] int a,
                                            [[maybe_unused]] int b)
{
    return 1;
}
[[maybe_unused]] static long SkoalaInitRedrawer([[maybe_unused]] panda::Method *m, [[maybe_unused]] int width,
                                                [[maybe_unused]] int height, [[maybe_unused]] float scale,
                                                [[maybe_unused]] long peer /*KNativePointer*/,
                                                [[maybe_unused]] long frame /*KNativePointer*/)
{
    return 1;
}
[[maybe_unused]] static long SkoalaPaint1nMake([[maybe_unused]] panda::Method *m)
{
    return 1;
}
[[maybe_unused]] static long SkoalaPictureRecorder1nBeginRecording(
    [[maybe_unused]] panda::Method *m, [[maybe_unused]] long ptr /*KNativePointer*/, [[maybe_unused]] float left,
    [[maybe_unused]] float top, [[maybe_unused]] float right, [[maybe_unused]] float bottom)
{
    return 1;
}
[[maybe_unused]] static long SkoalaPictureRecorder1nFinishRecordingAsDrawable(
    [[maybe_unused]] panda::Method *m, [[maybe_unused]] long ptr /*KNativePointer*/)
{
    return 1;
}
[[maybe_unused]] static long SkoalaPictureRecorder1nFinishRecordingAsPictureWithCull(
    [[maybe_unused]] panda::Method *m, [[maybe_unused]] long ptr /*KNativePointer*/, [[maybe_unused]] float left,
    [[maybe_unused]] float top, [[maybe_unused]] float right, [[maybe_unused]] float bottom)
{
    return 1;
}
[[maybe_unused]] static long SkoalaPictureRecorder1nMake([[maybe_unused]] panda::Method *m)
{
    return 1;
}
[[maybe_unused]] static long SkoalaParagraphParagraphBuilder1nMake(
    [[maybe_unused]] panda::Method *m, [[maybe_unused]] long paragraph_style_ptr /*KNativePointer*/,
    [[maybe_unused]] long font_collection_ptr /*KNativePointer*/)
{
    return 1;
}
[[maybe_unused]] static long SkoalaParagraphParagraphBuilder1nGetFinalizer([[maybe_unused]] panda::Method *m)
{
    return 1;
}
[[maybe_unused]] static long SkoalaParagraphParagraphBuilder1nBuild([[maybe_unused]] panda::Method *m,
                                                                    [[maybe_unused]] long ptr /*KNativePointer*/)
{
    return 1;
}
[[maybe_unused]] static long SkoalaParagraphFontCollection1nMake([[maybe_unused]] panda::Method *m)
{
    return 1;
}
[[maybe_unused]] static long SkoalaFontMgr1nDefault([[maybe_unused]] panda::Method *m)
{
    return 1;
}
[[maybe_unused]] static long SkoalaParagraphParagraphStyle1nMake([[maybe_unused]] panda::Method *m)
{
    return 1;
}
[[maybe_unused]] static long SkoalaParagraphTextStyle1nMake([[maybe_unused]] panda::Method *m)
{
    return 1;
}
[[maybe_unused]] static long SkoalaManagedString1nMake([[maybe_unused]] panda::Method *m,
                                                       [[maybe_unused]] char *text_str /*KStringPtr*/)
{
    return 1;
}
[[maybe_unused]] static long SkoalaPaint1nGetFinalizer([[maybe_unused]] panda::Method *m)
{
    return 1;
}
[[maybe_unused]] static long SkoalaImplRefCntGetFinalizer([[maybe_unused]] panda::Method *m)
{
    return 1;
}
[[maybe_unused]] static long SkoalaParagraphParagraphStyle1nGetFinalizer([[maybe_unused]] panda::Method *m)
{
    return 1;
}
[[maybe_unused]] static long SkoalaParagraphTextStyle1nGetFinalizer([[maybe_unused]] panda::Method *m)
{
    return 1;
}
[[maybe_unused]] static long SkoalaParagraphParagraph1nGetFinalizer([[maybe_unused]] panda::Method *m)
{
    return 1;
}
[[maybe_unused]] static long SkoalaManagedString1nGetFinalizer([[maybe_unused]] panda::Method *m)
{
    return 1;
}
[[maybe_unused]] static long GetPeerFactory([[maybe_unused]] panda::Method *m)
{
    return 1;
}
[[maybe_unused]] static long GetEngine([[maybe_unused]] panda::Method *m)
{
    return 1;
}
[[maybe_unused]] static int SkoalaGetFrameWidth([[maybe_unused]] panda::Method *m,
                                                [[maybe_unused]] long peer /*KNativePointer*/,
                                                [[maybe_unused]] long frame /*KNativePointer*/)
{
    return 1;
}
[[maybe_unused]] static int SkoalaGetFrameHeight([[maybe_unused]] panda::Method *m,
                                                 [[maybe_unused]] long peer /*KNativePointer*/,
                                                 [[maybe_unused]] long frame /*KNativePointer*/)
{
    return 1;
}
[[maybe_unused]] static int SkoalaCanvas1nSave([[maybe_unused]] panda::Method *m,
                                               [[maybe_unused]] long ptr /*KNativePointer*/)
{
    return 1;
}
[[maybe_unused]] static void SkoalaDrawPicture([[maybe_unused]] panda::Method *m,
                                               [[maybe_unused]] long picture /*KNativePointer*/,
                                               [[maybe_unused]] long data /*KNativePointer*/,
                                               [[maybe_unused]] void *cb /*any*/, [[maybe_unused]] bool sync)
{
}
[[maybe_unused]] static void SkoalaProvidePeerFactory([[maybe_unused]] panda::Method *m,
                                                      [[maybe_unused]] long func /*KNativePointer*/,
                                                      [[maybe_unused]] long arg /*KNativePointer*/)
{
}
[[maybe_unused]] static void SkoalaSetPlatformApi([[maybe_unused]] panda::Method *m, [[maybe_unused]] void *api /*any*/)
{
}
[[maybe_unused]] static void SkoalaCanvas1nDrawDrawable([[maybe_unused]] panda::Method *m,
                                                        [[maybe_unused]] long ptr /*KNativePointer*/,
                                                        [[maybe_unused]] long drawable_ptr /*KNativePointer*/,
                                                        [[maybe_unused]] long matrix_arr /*KFloatPtr*/)
{
}
[[maybe_unused]] static void SkoalaCanvas1nRestore([[maybe_unused]] panda::Method *m,
                                                   [[maybe_unused]] long ptr /*KNativePointer*/)
{
}
[[maybe_unused]] static void SkoalaPaint1nSetColor([[maybe_unused]] panda::Method *m,
                                                   [[maybe_unused]] long ptr /*KNativePointer*/,
                                                   [[maybe_unused]] int color)
{
}
[[maybe_unused]] static void SkoalaCanvas1nDrawOval([[maybe_unused]] panda::Method *m,
                                                    [[maybe_unused]] long canvas_ptr /*KNativePointer*/,
                                                    [[maybe_unused]] float left, [[maybe_unused]] float top,
                                                    [[maybe_unused]] float right, [[maybe_unused]] float bottom,
                                                    [[maybe_unused]] long paint_ptr /*KNativePointer*/)
{
}
[[maybe_unused]] static void SkoalaParagraphParagraphBuilder1nPushStyle(
    [[maybe_unused]] panda::Method *m, [[maybe_unused]] long ptr /*KNativePointer*/,
    [[maybe_unused]] long text_style_ptr /*KNativePointer*/)
{
}
[[maybe_unused]] static void SkoalaParagraphParagraphBuilder1nAddText([[maybe_unused]] panda::Method *m,
                                                                      [[maybe_unused]] long ptr /*KNativePointer*/,
                                                                      [[maybe_unused]] long text_string /*KStringPtr*/)
{
}
[[maybe_unused]] static void SkoalaParagraphFontCollection1nSetDefaultFontManager(
    [[maybe_unused]] panda::Method *m, [[maybe_unused]] long ptr /*KNativePointer*/,
    [[maybe_unused]] long font_manager_ptr /*KNativePointer*/,
    [[maybe_unused]] long default_family_name_str /*KStringPtr*/)
{
}
[[maybe_unused]] static void SkoalaParagraphParagraph1nLayout([[maybe_unused]] panda::Method *m,
                                                              [[maybe_unused]] long ptr /*KNativePointer*/,
                                                              [[maybe_unused]] float width)
{
}
[[maybe_unused]] static void SkoalaParagraphParagraph1nPaint([[maybe_unused]] panda::Method *m,
                                                             [[maybe_unused]] long ptr /*KNativePointer*/,
                                                             [[maybe_unused]] long canvas_ptr /*KNativePointer*/,
                                                             [[maybe_unused]] float x, [[maybe_unused]] float y)
{
}
[[maybe_unused]] static void SkoalaParagraphTextStyle1nSetColor([[maybe_unused]] panda::Method *m,
                                                                [[maybe_unused]] long ptr /*KNativePointer*/,
                                                                [[maybe_unused]] int color)
{
}
[[maybe_unused]] static void SkoalaParagraphTextStyle1nSetFontSize([[maybe_unused]] panda::Method *m,
                                                                   [[maybe_unused]] long ptr /*KNativePointer*/,
                                                                   [[maybe_unused]] float size)
{
}
[[maybe_unused]] static void SkoalaImplManagedInvokeFinalizer([[maybe_unused]] panda::Method *m,
                                                              [[maybe_unused]] long finalizer /*KNativePointer*/,
                                                              [[maybe_unused]] long obj /*KNativePointer*/)
{
}
[[maybe_unused]] static void SkoalaEnqueueRun([[maybe_unused]] panda::Method *m,
                                              [[maybe_unused]] long redrawer_peer_ptr /*KNativePointer*/)
{
}
// NOLINTEND(google-runtime-int)

static bool InitExports()
{
    std::map<std::string, void *> impls = {
        {"_skoala_createRedrawerPeer", reinterpret_cast<void *>(SkoalaCreateRedrawerPeer)},
        {"_skoala_drawPicture", reinterpret_cast<void *>(SkoalaDrawPicture)},
        {"_skoala_getFrame", reinterpret_cast<void *>(SkoalaGetFrame)},
        {"_skoala_getFrameWidth", reinterpret_cast<void *>(SkoalaGetFrameWidth)},
        {"_skoala_getFrameHeight", reinterpret_cast<void *>(SkoalaGetFrameHeight)},
        {"_skoala_initRedrawer", reinterpret_cast<void *>(SkoalaInitRedrawer)},
        {"_skoala_providePeerFactory", reinterpret_cast<void *>(SkoalaProvidePeerFactory)},
        {"_skoala_setPlatformAPI", reinterpret_cast<void *>(SkoalaSetPlatformApi)},
        {"_skoala_Canvas__1nDrawDrawable", reinterpret_cast<void *>(SkoalaCanvas1nDrawDrawable)},
        {"_skoala_Canvas__1nRestore", reinterpret_cast<void *>(SkoalaCanvas1nRestore)},
        {"_skoala_Canvas__1nSave", reinterpret_cast<void *>(SkoalaCanvas1nSave)},
        {"_skoala_Paint__1nMake", reinterpret_cast<void *>(SkoalaPaint1nMake)},
        {"_skoala_Paint__1nSetColor", reinterpret_cast<void *>(SkoalaPaint1nSetColor)},
        {"_skoala_PictureRecorder__1nBeginRecording", reinterpret_cast<void *>(SkoalaPictureRecorder1nBeginRecording)},
        {"_skoala_PictureRecorder__1nFinishRecordingAsDrawable",
         reinterpret_cast<void *>(SkoalaPictureRecorder1nFinishRecordingAsDrawable)},
        {"_skoala_PictureRecorder__1nFinishRecordingAsPictureWithCull",
         reinterpret_cast<void *>(SkoalaPictureRecorder1nFinishRecordingAsPictureWithCull)},
        {"_skoala_PictureRecorder__1nMake", reinterpret_cast<void *>(SkoalaPictureRecorder1nMake)},
        {"_skoala_Canvas__1nDrawOval", reinterpret_cast<void *>(SkoalaCanvas1nDrawOval)},
        {"_skoala_paragraph_ParagraphBuilder__1nMake", reinterpret_cast<void *>(SkoalaParagraphParagraphBuilder1nMake)},
        {"_skoala_paragraph_ParagraphBuilder__1nGetFinalizer",
         reinterpret_cast<void *>(SkoalaParagraphParagraphBuilder1nGetFinalizer)},
        {"_skoala_paragraph_ParagraphBuilder__1nPushStyle",
         reinterpret_cast<void *>(SkoalaParagraphParagraphBuilder1nPushStyle)},
        {"_skoala_paragraph_ParagraphBuilder__1nAddText",
         reinterpret_cast<void *>(SkoalaParagraphParagraphBuilder1nAddText)},
        {"_skoala_paragraph_ParagraphBuilder__1nBuild",
         reinterpret_cast<void *>(SkoalaParagraphParagraphBuilder1nBuild)},
        {"_skoala_paragraph_FontCollection__1nMake", reinterpret_cast<void *>(SkoalaParagraphFontCollection1nMake)},
        {"_skoala_paragraph_FontCollection__1nSetDefaultFontManager",
         reinterpret_cast<void *>(SkoalaParagraphFontCollection1nSetDefaultFontManager)},
        {"_skoala_FontMgr__1nDefault", reinterpret_cast<void *>(SkoalaFontMgr1nDefault)},
        {"_skoala_paragraph_ParagraphStyle__1nMake", reinterpret_cast<void *>(SkoalaParagraphParagraphStyle1nMake)},
        {"_skoala_paragraph_Paragraph__1nLayout", reinterpret_cast<void *>(SkoalaParagraphParagraph1nLayout)},
        {"_skoala_paragraph_Paragraph__1nPaint", reinterpret_cast<void *>(SkoalaParagraphParagraph1nPaint)},
        {"_skoala_paragraph_TextStyle__1nMake", reinterpret_cast<void *>(SkoalaParagraphTextStyle1nMake)},
        {"_skoala_paragraph_TextStyle__1nSetColor", reinterpret_cast<void *>(SkoalaParagraphTextStyle1nSetColor)},
        {"_skoala_paragraph_TextStyle__1nSetFontSize", reinterpret_cast<void *>(SkoalaParagraphTextStyle1nSetFontSize)},
        {"_skoala_ManagedString__1nMake", reinterpret_cast<void *>(SkoalaManagedString1nMake)},
        {"_skoala_Paint__1nGetFinalizer", reinterpret_cast<void *>(SkoalaPaint1nGetFinalizer)},
        {"_skoala_impl_Managed__invokeFinalizer", reinterpret_cast<void *>(SkoalaImplManagedInvokeFinalizer)},
        {"_skoala_impl_RefCnt__getFinalizer", reinterpret_cast<void *>(SkoalaImplRefCntGetFinalizer)},
        {"_skoala_paragraph_ParagraphStyle__1nGetFinalizer",
         reinterpret_cast<void *>(SkoalaParagraphParagraphStyle1nGetFinalizer)},
        {"_skoala_paragraph_TextStyle__1nGetFinalizer",
         reinterpret_cast<void *>(SkoalaParagraphTextStyle1nGetFinalizer)},
        {"_skoala_paragraph_Paragraph__1nGetFinalizer",
         reinterpret_cast<void *>(SkoalaParagraphParagraph1nGetFinalizer)},
        {"_skoala_ManagedString__1nGetFinalizer", reinterpret_cast<void *>(SkoalaManagedString1nGetFinalizer)},
        {"_getPeerFactory", reinterpret_cast<void *>(GetPeerFactory)},
        {"_getEngine", reinterpret_cast<void *>(GetEngine)},
        {"_skoala_enqueue_run", reinterpret_cast<void *>(SkoalaEnqueueRun)}};

    auto *thread = panda::ManagedThread::GetCurrent();
    panda::ScopedManagedCodeThread sj(thread);

    for (const auto &[name, impl] : impls) {
        if (!panda::ets::BindNative("LETSGLOBAL;", name.data(), impl)) {
            return false;
        }
    }

    return true;
}

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
TEST(EtsVMConfing, PeasINT)
{
    std::string stdlib_abc;
    std::string path_abc;

#if defined(STDLIB_ABC) && defined(PATH_ABC)
#define ETS_UNIT_STRING_STEP(s) #s
#define ETS_UNIT_STRING(s) ETS_UNIT_STRING_STEP(s)
    stdlib_abc = ETS_UNIT_STRING(STDLIB_ABC);
    path_abc = ETS_UNIT_STRING(PATH_ABC);
#undef ETS_UNIT_STRING
#undef ETS_UNIT_STRING_STEP
#endif

    ASSERT_FALSE(stdlib_abc.empty());
    ASSERT_FALSE(path_abc.empty());

    ASSERT_TRUE(panda::ets::CreateRuntime(stdlib_abc, path_abc, false, false));
    EXPECT_TRUE(InitExports());
    auto res = panda::ets::ExecuteMain();
    EXPECT_TRUE(res.first == true);
    EXPECT_TRUE(res.second == 0);
    ASSERT_TRUE(panda::ets::DestroyRuntime());
}

TEST(EtsVMConfing, PeasJIT)
{
    std::string stdlib_abc;
    std::string path_abc;

#if defined(STDLIB_ABC) && defined(PATH_ABC)
#define ETS_UNIT_STRING_STEP(s) #s
#define ETS_UNIT_STRING(s) ETS_UNIT_STRING_STEP(s)
    stdlib_abc = ETS_UNIT_STRING(STDLIB_ABC);
    path_abc = ETS_UNIT_STRING(PATH_ABC);
#undef ETS_UNIT_STRING
#undef ETS_UNIT_STRING_STEP
#endif

    ASSERT_FALSE(stdlib_abc.empty());
    ASSERT_FALSE(path_abc.empty());

    ASSERT_TRUE(panda::ets::CreateRuntime(stdlib_abc, path_abc, true, false));
    EXPECT_TRUE(InitExports());
    auto res = panda::ets::ExecuteMain();
    EXPECT_TRUE(res.first == true);
    EXPECT_TRUE(res.second == 0);
    ASSERT_TRUE(panda::ets::DestroyRuntime());
}

TEST(EtsVMConfing, PeasAOT)
{
#ifdef HOST_CROSSCOMPILING
    GTEST_SKIP();
#endif

    std::string stdlib_abc;
    std::string path_abc;
    std::string path_an;

#if defined(STDLIB_ABC) && defined(PATH_ABC)
#define ETS_UNIT_STRING_STEP(s) #s
#define ETS_UNIT_STRING(s) ETS_UNIT_STRING_STEP(s)
    stdlib_abc = ETS_UNIT_STRING(STDLIB_ABC);
    path_abc = ETS_UNIT_STRING(PATH_ABC);
    path_an = ETS_UNIT_STRING(PATH_AN);
#undef ETS_UNIT_STRING
#undef ETS_UNIT_STRING_STEP
#endif

    ASSERT_FALSE(stdlib_abc.empty());
    ASSERT_FALSE(path_abc.empty());
    ASSERT_FALSE(path_an.empty());

    ASSERT_TRUE(panda::ets::CreateRuntime(stdlib_abc, path_abc, false, true));
    EXPECT_TRUE(InitExports());
    auto res = panda::ets::ExecuteMain();
    EXPECT_TRUE(res.first == true);
    EXPECT_TRUE(res.second == 0);
    ASSERT_TRUE(panda::ets::DestroyRuntime());
}
// NOLINTEND(cppcoreguidelines-macro-usage)

}  // namespace panda::ets::test
