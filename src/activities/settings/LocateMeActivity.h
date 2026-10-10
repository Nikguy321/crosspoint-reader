#pragma once

#include <I18n.h>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "network/GeolocateProtocol.h"
#include "network/LocateRun.h"

/**
 * Display > Sleep Screen Cards > Locate Me (X4 Pro): finds the sleep-card location instead of
 * typed coordinates.
 *
 *   Intro   what will be asked and sent where; nothing leaves the reader before Locate is tapped.
 *   Join    a saved Wi-Fi network through WifiSelectionActivity's auto-connect (the last network,
 *           then any saved one in range; the network list only when none is).
 *   Work    network/LocateRun: the phone's GPS over its hotspot, else nearby Wi-Fi when two
 *           beaconDB lookups agree. Never an internet-address lookup.
 *   Result  the place and how sure it is: Save stores it (with its source, accuracy and date),
 *           Cancel leaves the settings as they were.
 *   Failed  why, in one plain line (what the phone and the Wi-Fi each said); when the network may
 *           not reach the internet, Choose Wi-Fi Network opens the network list and tries again on
 *           the one picked.
 *
 * The radio starts only through RadioPower (inside WifiSelectionActivity and the scan). After the
 * lookups the RF is stopped for the result screen, or the radio is turned fully off on a failure
 * (so a network picked afterwards starts it afresh). Leaving reboots back to Sleep Screen Cards
 * to free the Wi-Fi driver's heap, as every network activity does. Nothing runs in the background.
 */
class LocateMeActivity final : public Activity, private UiAppHost {
 public:
  explicit LocateMeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  // Only the lookup spins the loop; the screens that wait for a tap may light-sleep.
  bool skipLoopDelay() override { return state == State::Working; }
  bool preventAutoSleep() override { return state == State::Working; }
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t { Intro, Joining, Working, Result, Failed };
  // NoLocation: the phone and Wi-Fi both came up empty (phone/wifi say how).
  enum class Failure : uint8_t { NoWifi, NoLocation, NoMemory, SaveFailed };

  State state = State::Intro;
  Failure failure = Failure::NoLocation;
  // What each source said, for the NoLocation screen.
  LocateRun::PhoneVerdict phoneVerdict = LocateRun::PhoneVerdict::NotFound;
  geolocate::WifiVerdict wifiVerdict = geolocate::WifiVerdict::NotAsked;
  StrId status = StrId::STR_LOCATE_ASKING_PHONE;
  int selected = 0;  // action row under the physical-button cursor
  // This activity brought the radio up (and so ends with the reboot).
  bool radioStarted = false;
  // The network list was opened from the failure screen: backing out of it returns there.
  bool pickingNetwork = false;
  geolocate::Fix fix;

  static void screenFn(UiScreen& screen, void* user);
  static void onRow(const freeink::ui::ActionEvent& event, void* user);
  void buildScreen(UiScreen& screen);
  int actionCount() const;
  void activate(int row);
  void startLocate();
  void chooseNetwork();
  void joinNetwork(bool autoConnect);
  void onWifiDone(bool connected);
  void runLookup();
  static void onStep(LocateRun::Step step, void* user);
  // The failure screen offers another network (the joined one may not reach the internet).
  bool offerNetwork() const;
  void radioOff();
  void showStatus(StrId next);
  void fail(Failure why);
  void save();
};
