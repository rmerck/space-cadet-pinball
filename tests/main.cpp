#include <cstdio>
#include <cstdlib>
#include <exception>

#include "Check.hpp"
#include "Suites.hpp"

namespace {

void runSuites() {
  testAcceptance();
  testArgs();
  testAssetName();
  testBumpers();
  testElements();
  testFixedTimestep();
  testFlippers();
  testFormat();
  testGame();
  testHighScores();
  testHoles();
  testLightShow();
  testLaunch();
  testCourses();
  testMultiball();
  testNudge();
  testPhysics();
  testPlunger();
  testPngHeader();
  testRuleFuzz();
  testRules();
  testScreenLayout();
  testSha256();
  testMusic();
  testSound();
  testTableGeometry();
  testTargets();
  testTouchControls();
}

}  // namespace

int main() {
  // An exception from a suite is a failure with a name, not a bare abort.
  try {
    runSuites();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "core_tests: uncaught exception: %s\n", e.what());
    return EXIT_FAILURE;
  } catch (...) {
    std::fputs("core_tests: uncaught exception\n", stderr);
    return EXIT_FAILURE;
  }

  if (pinball_test::failures() != 0) {
    std::fprintf(stderr, "core_tests: %d failure(s)\n", pinball_test::failures());
    return EXIT_FAILURE;
  }
  if (pinball_test::checks() == 0) {
    std::fputs("core_tests: no checks ran\n", stderr);
    return EXIT_FAILURE;
  }
  std::printf("core_tests: all %ld checks passed\n", pinball_test::checks());
  return EXIT_SUCCESS;
}
