#pragma once

// Every test suite. Each is defined in its own file, which includes this
// header, and is called from main.cpp. A declared suite with no definition
// fails to link.
void testAcceptance();
void testArgs();
void testAssetName();
void testBumpers();
void testElements();
void testFixedTimestep();
void testFlippers();
void testFormat();
void testGame();
void testHighScores();
void testHoles();
void testLightShow();
void testLaunch();
void testCourses();
void testMultiball();
void testNudge();
void testPhysics();
void testPlunger();
void testPngHeader();
void testRuleFuzz();
void testRules();
void testScreenLayout();
void testSha256();
void testMusic();
void testSound();
void testTableGeometry();
void testTargets();
void testTouchControls();
