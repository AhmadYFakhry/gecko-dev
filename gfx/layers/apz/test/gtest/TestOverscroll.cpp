/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "APZCBasicTester.h"
#include "APZTestCommon.h"

#include "InputUtils.h"

class APZCOverscrollTester : public APZCBasicTester {
 public:
  explicit APZCOverscrollTester(
      AsyncPanZoomController::GestureBehavior aGestureBehavior =
          AsyncPanZoomController::DEFAULT_GESTURES)
      : APZCBasicTester(aGestureBehavior) {}

 protected:
  void TestOverscroll() {
    // Pan sufficiently to hit overscroll behavior
    PanIntoOverscroll();

    // Check that we recover from overscroll via an animation.
    ParentLayerPoint expectedScrollOffset(0, GetScrollRange().YMost());
    SampleAnimationUntilRecoveredFromOverscroll(expectedScrollOffset);
  }

  void PanIntoOverscroll() {
    int touchStart = 500;
    int touchEnd = 10;
    Pan(apzc, touchStart, touchEnd);
    EXPECT_TRUE(apzc->IsOverscrolled());
  }

  /**
   * Sample animations until we recover from overscroll.
   * @param aExpectedScrollOffset the expected reported scroll offset
   *                              throughout the animation
   */
  void SampleAnimationUntilRecoveredFromOverscroll(
      const ParentLayerPoint& aExpectedScrollOffset) {
    const TimeDuration increment = TimeDuration::FromMilliseconds(1);
    bool recoveredFromOverscroll = false;
    ParentLayerPoint pointOut;
    AsyncTransform viewTransformOut;
    while (apzc->SampleContentTransformForFrame(&viewTransformOut, pointOut)) {
      // The reported scroll offset should be the same throughout.
      EXPECT_EQ(aExpectedScrollOffset, pointOut);

      // Trigger computation of the overscroll tranform, to make sure
      // no assetions fire during the calculation.
      apzc->GetOverscrollTransform(AsyncPanZoomController::eForHitTesting);

      if (!apzc->IsOverscrolled()) {
        recoveredFromOverscroll = true;
      }

      mcc->AdvanceBy(increment);
    }
    EXPECT_TRUE(recoveredFromOverscroll);
    apzc->AssertStateIsReset();
  }

};

#ifndef MOZ_WIDGET_ANDROID  // Currently fails on Android
TEST_F(APZCOverscrollTester, FlingIntoOverscroll) {
  // Enable overscrolling.
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);
  SCOPED_GFX_PREF_FLOAT("apz.fling_min_velocity_threshold", 0.0f);

  // Scroll down by 25 px. Don't fling for simplicity.
  Pan(apzc, 50, 25, PanOptions::NoFling);

  // Now scroll back up by 20px, this time flinging after.
  // The fling should cover the remaining 5 px of room to scroll, then
  // go into overscroll, and finally snap-back to recover from overscroll.
  Pan(apzc, 25, 45);
  const TimeDuration increment = TimeDuration::FromMilliseconds(1);
  bool reachedOverscroll = false;
  bool recoveredFromOverscroll = false;
  while (apzc->AdvanceAnimations(mcc->GetSampleTime())) {
    if (!reachedOverscroll && apzc->IsOverscrolled()) {
      reachedOverscroll = true;
    }
    if (reachedOverscroll && !apzc->IsOverscrolled()) {
      recoveredFromOverscroll = true;
    }
    mcc->AdvanceBy(increment);
  }
  EXPECT_TRUE(reachedOverscroll);
  EXPECT_TRUE(recoveredFromOverscroll);
}
#endif

TEST_F(APZCOverscrollTester, PanningTransformNotifications) {
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);

  // Scroll down by 25 px. Ensure we only get one set of
  // state change notifications.
  //
  // Then, scroll back up by 20px, this time flinging after.
  // The fling should cover the remaining 5 px of room to scroll, then
  // go into overscroll, and finally snap-back to recover from overscroll.
  // Again, ensure we only get one set of state change notifications for
  // this entire procedure.

  MockFunction<void(std::string checkPointName)> check;
  {
    InSequence s;
    EXPECT_CALL(check, Call("Simple pan"));
    EXPECT_CALL(*mcc,
                NotifyAPZStateChange(
                    _, GeckoContentController::APZStateChange::eStartTouch, _))
        .Times(1);
    EXPECT_CALL(
        *mcc,
        NotifyAPZStateChange(
            _, GeckoContentController::APZStateChange::eTransformBegin, _))
        .Times(1);
    EXPECT_CALL(
        *mcc, NotifyAPZStateChange(
                  _, GeckoContentController::APZStateChange::eStartPanning, _))
        .Times(1);
    EXPECT_CALL(*mcc,
                NotifyAPZStateChange(
                    _, GeckoContentController::APZStateChange::eEndTouch, _))
        .Times(1);
    EXPECT_CALL(
        *mcc, NotifyAPZStateChange(
                  _, GeckoContentController::APZStateChange::eTransformEnd, _))
        .Times(1);
    EXPECT_CALL(check, Call("Complex pan"));
    EXPECT_CALL(*mcc,
                NotifyAPZStateChange(
                    _, GeckoContentController::APZStateChange::eStartTouch, _))
        .Times(1);
    EXPECT_CALL(
        *mcc,
        NotifyAPZStateChange(
            _, GeckoContentController::APZStateChange::eTransformBegin, _))
        .Times(1);
    EXPECT_CALL(
        *mcc, NotifyAPZStateChange(
                  _, GeckoContentController::APZStateChange::eStartPanning, _))
        .Times(1);
    EXPECT_CALL(*mcc,
                NotifyAPZStateChange(
                    _, GeckoContentController::APZStateChange::eEndTouch, _))
        .Times(1);
    EXPECT_CALL(
        *mcc, NotifyAPZStateChange(
                  _, GeckoContentController::APZStateChange::eTransformEnd, _))
        .Times(1);
    EXPECT_CALL(check, Call("Done"));
  }

  check.Call("Simple pan");
  Pan(apzc, 50, 25, PanOptions::NoFling);
  check.Call("Complex pan");
  Pan(apzc, 25, 45);
  apzc->AdvanceAnimationsUntilEnd();
  check.Call("Done");
}

#ifndef MOZ_WIDGET_ANDROID  // Currently fails on Android
TEST_F(APZCOverscrollTester, OverScrollPanning) {
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);

  TestOverscroll();
}
#endif

#ifndef MOZ_WIDGET_ANDROID  // Currently fails on Android
// Tests that an overscroll animation doesn't trigger an assertion failure
// in the case where a sample has a velocity of zero.
TEST_F(APZCOverscrollTester, OverScroll_Bug1152051a) {
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);

  // Doctor the prefs to make the velocity zero at the end of the first sample.

  // This ensures our incoming velocity to the overscroll animation is
  // a round(ish) number, 4.9 (that being the distance of the pan before
  // overscroll, which is 500 - 10 = 490 pixels, divided by the duration of
  // the pan, which is 100 ms).
  SCOPED_GFX_PREF_FLOAT("apz.fling_friction", 0);

  TestOverscroll();
}
#endif

#ifndef MOZ_WIDGET_ANDROID  // Currently fails on Android
// Tests that ending an overscroll animation doesn't leave around state that
// confuses the next overscroll animation.
TEST_F(APZCOverscrollTester, OverScroll_Bug1152051b) {
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);
  SCOPED_GFX_PREF_FLOAT("apz.overscroll.stop_distance_threshold", 0.1f);

  // Pan sufficiently to hit overscroll behavior
  PanIntoOverscroll();

  // Sample animations once, to give the fling animation started on touch-up
  // a chance to realize it's overscrolled, and schedule a call to
  // HandleFlingOverscroll().
  SampleAnimationOnce();

  // This advances the time and runs the HandleFlingOverscroll task scheduled in
  // the previous call, which starts an overscroll animation. It then samples
  // the overscroll animation once, to get it to initialize the first overscroll
  // sample.
  SampleAnimationOnce();

  // Do a touch-down to cancel the overscroll animation, and then a touch-up
  // to schedule a new one since we're still overscrolled. We don't pan because
  // panning can trigger functions that clear the overscroll animation state
  // in other ways.
  APZEventResult result = TouchDown(apzc, ScreenIntPoint(10, 10), mcc->Time());
  if (StaticPrefs::layout_css_touch_action_enabled() &&
      result.GetStatus() != nsEventStatus_eConsumeNoDefault) {
    SetDefaultAllowedTouchBehavior(apzc, result.mInputBlockId);
  }
  TouchUp(apzc, ScreenIntPoint(10, 10), mcc->Time());

  // Sample the second overscroll animation to its end.
  // If the ending of the first overscroll animation fails to clear state
  // properly, this will assert.
  ParentLayerPoint expectedScrollOffset(0, GetScrollRange().YMost());
  SampleAnimationUntilRecoveredFromOverscroll(expectedScrollOffset);
}
#endif

#ifndef MOZ_WIDGET_ANDROID  // Currently fails on Android
// Tests that the page doesn't get stuck in an
// overscroll animation after a low-velocity pan.
TEST_F(APZCOverscrollTester, OverScrollAfterLowVelocityPan_Bug1343775) {
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);

  // Pan into overscroll with a velocity less than the
  // apz.fling_min_velocity_threshold preference.
  Pan(apzc, 10, 30);

  EXPECT_TRUE(apzc->IsOverscrolled());

  apzc->AdvanceAnimationsUntilEnd();

  // Check that we recovered from overscroll.
  EXPECT_FALSE(apzc->IsOverscrolled());
}
#endif

#ifndef MOZ_WIDGET_ANDROID  // Currently fails on Android
TEST_F(APZCOverscrollTester, OverScrollAbort) {
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);

  // Pan sufficiently to hit overscroll behavior
  int touchStart = 500;
  int touchEnd = 10;
  Pan(apzc, touchStart, touchEnd);
  EXPECT_TRUE(apzc->IsOverscrolled());

  ParentLayerPoint pointOut;
  AsyncTransform viewTransformOut;

  // This sample call will run to the end of the fling animation
  // and will schedule the overscroll animation.
  apzc->SampleContentTransformForFrame(&viewTransformOut, pointOut,
                                       TimeDuration::FromMilliseconds(10000));
  EXPECT_TRUE(apzc->IsOverscrolled());

  // At this point, we have an active overscroll animation.
  // Check that cancelling the animation clears the overscroll.
  apzc->CancelAnimation();
  EXPECT_FALSE(apzc->IsOverscrolled());
  apzc->AssertStateIsReset();
}
#endif

#ifndef MOZ_WIDGET_ANDROID  // Currently fails on Android
TEST_F(APZCOverscrollTester, OverScrollPanningAbort) {
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);

  // Pan sufficiently to hit overscroll behaviour. Keep the finger down so
  // the pan does not end.
  int touchStart = 500;
  int touchEnd = 10;
  Pan(apzc, touchStart, touchEnd, PanOptions::KeepFingerDown);
  EXPECT_TRUE(apzc->IsOverscrolled());

  // Check that calling CancelAnimation() while the user is still panning
  // (and thus no fling or snap-back animation has had a chance to start)
  // clears the overscroll.
  apzc->CancelAnimation();
  EXPECT_FALSE(apzc->IsOverscrolled());
  apzc->AssertStateIsReset();
}
#endif

#ifndef MOZ_WIDGET_ANDROID  // Maybe fails on Android
TEST_F(APZCOverscrollTester, OverscrollByVerticalPanGestures) {
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);

  PanGesture(PanGestureInput::PANGESTURE_START, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, -2), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, -10), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, -2), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_END, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 0), mcc->Time());

  EXPECT_TRUE(apzc->IsOverscrolled());

  // Check that we recover from overscroll via an animation.
  ParentLayerPoint expectedScrollOffset(0, 0);
  SampleAnimationUntilRecoveredFromOverscroll(expectedScrollOffset);
}
#endif

#ifndef MOZ_WIDGET_ANDROID  // Currently fails on Android
TEST_F(APZCOverscrollTester, OverscrollByVerticalAndHorizontalPanGestures) {
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);

  PanGesture(PanGestureInput::PANGESTURE_START, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, -2), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, -10), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, -2), mcc->Time());

  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(-10, 0), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(-2, 0), mcc->Time());

  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_END, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 0), mcc->Time());

  EXPECT_TRUE(apzc->IsOverscrolled());

  // Check that we recover from overscroll via an animation.
  ParentLayerPoint expectedScrollOffset(0, 0);
  SampleAnimationUntilRecoveredFromOverscroll(expectedScrollOffset);
}
#endif

#ifndef MOZ_WIDGET_ANDROID  // Currently fails on Android
TEST_F(APZCOverscrollTester, OverscrollByPanMomentumGestures) {
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);

  PanGesture(PanGestureInput::PANGESTURE_START, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 2), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 10), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 2), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_END, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 0), mcc->Time());

  // Make sure we are not yet in overscrolled region.
  EXPECT_TRUE(!apzc->IsOverscrolled());

  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMSTART, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, 0), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, 200), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, 100), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, 2), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMEND, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, 0), mcc->Time());

  EXPECT_TRUE(apzc->IsOverscrolled());

  // Check that we recover from overscroll via an animation.
  ParentLayerPoint expectedScrollOffset(0, GetScrollRange().YMost());
  SampleAnimationUntilRecoveredFromOverscroll(expectedScrollOffset);
}
#endif

#ifndef MOZ_WIDGET_ANDROID  // Currently fails on Android
TEST_F(APZCOverscrollTester, IgnoreMomemtumDuringOverscroll) {
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);

  float yMost = GetScrollRange().YMost();
  PanGesture(PanGestureInput::PANGESTURE_START, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, yMost / 10), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, yMost), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, yMost / 10), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_END, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 0), mcc->Time());

  // Make sure we've started an overscroll animation.
  EXPECT_TRUE(apzc->IsOverscrolled());
  EXPECT_TRUE(apzc->IsOverscrollAnimationRunning());

  // And check the overscrolled transform value before/after calling PanGesture
  // to make sure the overscroll amount isn't affected by momentum events.
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  AsyncTransformComponentMatrix overscrolledTransform =
      apzc->GetOverscrollTransform(AsyncPanZoomController::eForHitTesting);
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMSTART, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, 0), mcc->Time());
  EXPECT_EQ(overscrolledTransform, apzc->GetOverscrollTransform(
                                       AsyncPanZoomController::eForHitTesting));

  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  overscrolledTransform =
      apzc->GetOverscrollTransform(AsyncPanZoomController::eForHitTesting);
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, 200), mcc->Time());
  EXPECT_EQ(overscrolledTransform, apzc->GetOverscrollTransform(
                                       AsyncPanZoomController::eForHitTesting));

  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  overscrolledTransform =
      apzc->GetOverscrollTransform(AsyncPanZoomController::eForHitTesting);
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, 100), mcc->Time());
  EXPECT_EQ(overscrolledTransform, apzc->GetOverscrollTransform(
                                       AsyncPanZoomController::eForHitTesting));

  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  overscrolledTransform =
      apzc->GetOverscrollTransform(AsyncPanZoomController::eForHitTesting);
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, 2), mcc->Time());
  EXPECT_EQ(overscrolledTransform, apzc->GetOverscrollTransform(
                                       AsyncPanZoomController::eForHitTesting));

  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  overscrolledTransform =
      apzc->GetOverscrollTransform(AsyncPanZoomController::eForHitTesting);
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMEND, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, 0), mcc->Time());
  EXPECT_EQ(overscrolledTransform, apzc->GetOverscrollTransform(
                                       AsyncPanZoomController::eForHitTesting));

  // Check that we've recovered from overscroll via an animation.
  ParentLayerPoint expectedScrollOffset(0, GetScrollRange().YMost());
  SampleAnimationUntilRecoveredFromOverscroll(expectedScrollOffset);
}
#endif

#ifndef MOZ_WIDGET_ANDROID  // Currently fails on Android
TEST_F(APZCOverscrollTester, VerticalOnlyOverscroll) {
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);

  // Make the content scrollable only vertically.
  ScrollMetadata metadata;
  FrameMetrics& metrics = metadata.GetMetrics();
  metrics.SetCompositionBounds(ParentLayerRect(0, 0, 100, 100));
  metrics.SetScrollableRect(CSSRect(0, 0, 100, 1000));
  apzc->SetFrameMetrics(metrics);

  // Scroll up into overscroll a bit.
  PanGesture(PanGestureInput::PANGESTURE_START, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(-2, -2), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(-10, -10), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(-2, -2), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_END, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 0), mcc->Time());
  // Now it's overscrolled.
  EXPECT_TRUE(apzc->IsOverscrolled());
  AsyncTransformComponentMatrix overscrolledTransform =
      apzc->GetOverscrollTransform(AsyncPanZoomController::eForHitTesting);
  // The overscroll shouldn't happen horizontally.
  EXPECT_TRUE(overscrolledTransform._41 == 0);
  // Happens only vertically.
  EXPECT_TRUE(overscrolledTransform._42 != 0);

  // Send pan momentum events including horizontal bits.
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMSTART, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, 0), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(-10, -100), mcc->Time());
  overscrolledTransform =
      apzc->GetOverscrollTransform(AsyncPanZoomController::eForHitTesting);
  // The overscroll shouldn't happen horizontally.
  EXPECT_TRUE(overscrolledTransform._41 == 0);
  EXPECT_TRUE(overscrolledTransform._42 != 0);

  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(-5, -50), mcc->Time());
  overscrolledTransform =
      apzc->GetOverscrollTransform(AsyncPanZoomController::eForHitTesting);
  EXPECT_TRUE(overscrolledTransform._41 == 0);
  EXPECT_TRUE(overscrolledTransform._42 != 0);

  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, -2), mcc->Time());
  overscrolledTransform =
      apzc->GetOverscrollTransform(AsyncPanZoomController::eForHitTesting);
  EXPECT_TRUE(overscrolledTransform._41 == 0);
  EXPECT_TRUE(overscrolledTransform._42 != 0);

  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMEND, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, 0), mcc->Time());
  overscrolledTransform =
      apzc->GetOverscrollTransform(AsyncPanZoomController::eForHitTesting);
  EXPECT_TRUE(overscrolledTransform._41 == 0);
  EXPECT_TRUE(overscrolledTransform._42 != 0);

  // Check that we recover from overscroll via an animation.
  ParentLayerPoint expectedScrollOffset(0, 0);
  SampleAnimationUntilRecoveredFromOverscroll(expectedScrollOffset);
}
#endif

#ifndef MOZ_WIDGET_ANDROID  // Currently fails on Android
TEST_F(APZCOverscrollTester, VerticalOnlyOverscrollByPanMomentum) {
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);

  // Make the content scrollable only vertically.
  ScrollMetadata metadata;
  FrameMetrics& metrics = metadata.GetMetrics();
  metrics.SetCompositionBounds(ParentLayerRect(0, 0, 100, 100));
  metrics.SetScrollableRect(CSSRect(0, 0, 100, 1000));
  // Scrolls the content down a bit.
  metrics.SetVisualScrollOffset(CSSPoint(0, 50));
  apzc->SetFrameMetrics(metrics);

  // Scroll up a bit where overscroll will not happen.
  PanGesture(PanGestureInput::PANGESTURE_START, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, -2), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, -10), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, -2), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_END, apzc, ScreenIntPoint(50, 80),
             ScreenPoint(0, 0), mcc->Time());

  // Make sure it's not yet overscrolled.
  EXPECT_TRUE(!apzc->IsOverscrolled());

  // Send pan momentum events including horizontal bits.
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMSTART, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, 0), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(-10, -100), mcc->Time());
  // Now it's overscrolled.
  EXPECT_TRUE(apzc->IsOverscrolled());

  AsyncTransformComponentMatrix overscrolledTransform =
      apzc->GetOverscrollTransform(AsyncPanZoomController::eForHitTesting);
  // But the overscroll shouldn't happen horizontally.
  EXPECT_TRUE(overscrolledTransform._41 == 0);
  // Happens only vertically.
  EXPECT_TRUE(overscrolledTransform._42 != 0);

  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(-5, -50), mcc->Time());
  overscrolledTransform =
      apzc->GetOverscrollTransform(AsyncPanZoomController::eForHitTesting);
  EXPECT_TRUE(overscrolledTransform._41 == 0);
  EXPECT_TRUE(overscrolledTransform._42 != 0);

  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, -2), mcc->Time());
  overscrolledTransform =
      apzc->GetOverscrollTransform(AsyncPanZoomController::eForHitTesting);
  EXPECT_TRUE(overscrolledTransform._41 == 0);
  EXPECT_TRUE(overscrolledTransform._42 != 0);

  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMEND, apzc,
             ScreenIntPoint(50, 80), ScreenPoint(0, 0), mcc->Time());
  overscrolledTransform =
      apzc->GetOverscrollTransform(AsyncPanZoomController::eForHitTesting);
  EXPECT_TRUE(overscrolledTransform._41 == 0);
  EXPECT_TRUE(overscrolledTransform._42 != 0);

  // Check that we recover from overscroll via an animation.
  ParentLayerPoint expectedScrollOffset(0, 0);
  SampleAnimationUntilRecoveredFromOverscroll(expectedScrollOffset);
}
#endif

#ifndef MOZ_WIDGET_ANDROID  // Currently fails on Android
TEST_F(APZCOverscrollTester, DisallowOverscrollInSingleLineTextControl) {
  SCOPED_GFX_PREF_BOOL("apz.overscroll.enabled", true);

  // Create a horizontal scrollable frame with `vertical disregarded direction`.
  ScrollMetadata metadata;
  FrameMetrics& metrics = metadata.GetMetrics();
  metrics.SetCompositionBounds(ParentLayerRect(0, 0, 100, 10));
  metrics.SetScrollableRect(CSSRect(0, 0, 1000, 10));
  apzc->SetFrameMetrics(metrics);
  metadata.SetDisregardedDirection(Some(ScrollDirection::eVertical));
  apzc->NotifyLayersUpdated(metadata, /*aIsFirstPaint=*/false,
                            /*aThisLayerTreeUpdated=*/true);

  // Try to overscroll up and left with pan gestures.
  PanGesture(PanGestureInput::PANGESTURE_START, apzc, ScreenIntPoint(50, 5),
             ScreenPoint(-2, -2), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 5),
             ScreenPoint(-10, -10), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_PAN, apzc, ScreenIntPoint(50, 5),
             ScreenPoint(-2, -2), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_END, apzc, ScreenIntPoint(50, 5),
             ScreenPoint(0, 0), mcc->Time());

  // No overscrolling should happen.
  EXPECT_TRUE(!apzc->IsOverscrolled());

  // Send pan momentum events too.
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMSTART, apzc,
             ScreenIntPoint(50, 5), ScreenPoint(0, 0), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 5), ScreenPoint(-100, -100), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 5), ScreenPoint(-50, -50), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMPAN, apzc,
             ScreenIntPoint(50, 5), ScreenPoint(-2, -2), mcc->Time());
  mcc->AdvanceByMillis(5);
  apzc->AdvanceAnimations(mcc->GetSampleTime());
  PanGesture(PanGestureInput::PANGESTURE_MOMENTUMEND, apzc,
             ScreenIntPoint(50, 5), ScreenPoint(0, 0), mcc->Time());
  // No overscrolling should happen either.
  EXPECT_TRUE(!apzc->IsOverscrolled());
}
#endif
