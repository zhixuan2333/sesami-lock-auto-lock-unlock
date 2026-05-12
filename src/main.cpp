/*
 * libasesame3btサンプル
 * BluetoothスキャンしてSesameを探す場合
 */
#include <Arduino.h>
#include <Sesame.h>
#include <SesameClient.h>
#include <SesameScanner.h>
#include <string>
#include <esp_sleep.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <Preferences.h>
// Sesame鍵情報設定用インクルードファイル
#if __has_include("mysesame-config.h")
#include "mysesame-config.h"
#endif

#if !defined(SESAME_SECRET)
#define SESAME_SECRET "**REPLACE**"
#endif
#if !defined(SESAME_PK)
#define SESAME_PK "**REPLACE**"
#endif
#if !defined(SESAME_MODEL)
#define SESAME_MODEL Sesame::model_t::sesame_3
#endif

#define RTC_RST D8
#define RTC_IO  D9
#define RTC_SCL D10

const char *sesame_pk = SESAME_PK;
const char *sesame_sec = SESAME_SECRET;

using libsesame3bt::Sesame;
using libsesame3bt::SesameClient;
using libsesame3bt::SesameInfo;
using libsesame3bt::SesameScanner;

SesameClient client{};
ThreeWire rtcWire(RTC_IO, RTC_SCL, RTC_RST);
RtcDS1302<ThreeWire> Rtc(rtcWire);
Preferences prefs;

// スケジュール設定 (NVSに永続化、デフォルト: unlock=8:00, lock=20:00)
uint8_t unlock_hour = 8, unlock_min = 0;
uint8_t lock_hour = 20, lock_min = 0;

void loadSchedule()
{
  prefs.begin("sched", true);
  unlock_hour = prefs.getUChar("uh", 8);
  unlock_min  = prefs.getUChar("um", 0);
  lock_hour   = prefs.getUChar("lh", 20);
  lock_min    = prefs.getUChar("lm", 0);
  prefs.end();
}

void saveSchedule()
{
  prefs.begin("sched", false);
  prefs.putUChar("uh", unlock_hour);
  prefs.putUChar("um", unlock_min);
  prefs.putUChar("lh", lock_hour);
  prefs.putUChar("lm", lock_min);
  prefs.end();
}

static std::string
model_str(Sesame::model_t model)
{
  switch (model)
  {
  case Sesame::model_t::sesame_3:        return "SESAME 3";
  case Sesame::model_t::wifi_2:          return "Wi-Fi Module 2";
  case Sesame::model_t::sesame_bot:      return "SESAME bot";
  case Sesame::model_t::sesame_bike:     return "SESAME Cycle";
  case Sesame::model_t::sesame_4:        return "SESAME 4";
  case Sesame::model_t::sesame_5:        return "SESAME 5";
  case Sesame::model_t::sesame_5_pro:    return "SESAME 5 PRO";
  case Sesame::model_t::sesame_touch:    return "SESAME TOUCH";
  case Sesame::model_t::sesame_touch_pro:return "SESAME TOUCH PRO";
  case Sesame::model_t::sesame_bike_2:   return "SESAME Cycle 2";
  case Sesame::model_t::remote:          return "Remote";
  case Sesame::model_t::remote_nano:     return "Remote nano";
  case Sesame::model_t::sesame_bot_2:    return "SESAME Bot 2";
  case Sesame::model_t::sesame_face:     return "SESAME Face";
  case Sesame::model_t::sesame_face_pro: return "SESAME Face PRO";
  case Sesame::model_t::sesame_6:        return "SESAME 6";
  case Sesame::model_t::sesame_6_pro:    return "SESAME 6 PRO";
  default:
    return "UNKNOWN(" + std::to_string(static_cast<int8_t>(model)) + ")";
  }
}

void scan_and_init()
{
  SesameScanner &scanner = SesameScanner::get();
  Serial.println("Scanning 10 seconds");
  std::vector<SesameInfo> results;
  scanner.scan(10'000, [&results](SesameScanner &_scanner, const SesameInfo *_info) {
    if (_info) {
      Serial.printf("model=%s,addr=%s,UUID=%s,registered=%u\n",
        model_str(_info->model).c_str(), _info->address.toString().c_str(),
        _info->uuid.toString().c_str(), _info->flags.registered);
      results.push_back(*_info);
    }
  });
  Serial.printf("%u devices found\n", results.size());
  auto found = std::find_if(results.cbegin(), results.cend(),
    [](auto &it) { return it.model == SESAME_MODEL && it.flags.registered; });
  if (found != results.cend()) {
    Serial.printf("Using %s (%s)\n", found->uuid.toString().c_str(), model_str(found->model).c_str());
    if (!client.begin(found->address, found->model)) {
      Serial.println("Failed to begin");
      return;
    }
    if (!client.set_keys(sesame_pk, sesame_sec))
      Serial.println("Failed to set keys");
  } else {
    Serial.println("No usable Sesame found");
  }
}

bool connected = false;
SesameClient::state_t sesame_state;

RTC_DATA_ATTR uint8_t last_action_day = 255;
RTC_DATA_ATTR bool unlocked_today = false;
RTC_DATA_ATTR bool locked_today = false;

bool action_unlock = false;
bool action_lock = false;

void state_update(SesameClient &client, SesameClient::state_t state)
{
  sesame_state = state;
  switch (state)
  {
  case SesameClient::state_t::idle:          Serial.println("State: idle");          break;
  case SesameClient::state_t::connected:     Serial.println("State: connected");     break;
  case SesameClient::state_t::authenticating:Serial.println("State: authenticating");break;
  case SesameClient::state_t::active:        Serial.println("State: active");        break;
  default:
    Serial.printf("State: Unknown(%u)\n", static_cast<uint8_t>(state));
    break;
  }
}

void initRtc()
{
  Rtc.Begin();
  if (Rtc.GetIsWriteProtected())
    Rtc.SetIsWriteProtected(false);
  if (!Rtc.GetIsRunning())
    Rtc.SetIsRunning(true);
  // 時刻はシリアルシェルから設定する
}

// DayOfWeek(): 0=日, 1=月, 2=火, 3=水, 4=木, 5=金, 6=土
bool isWeekend(const RtcDateTime &dt)
{
  uint8_t dow = dt.DayOfWeek();
  return dow == 0 || dow == 6; // 日曜 or 土曜
}

// ---------- シリアルシェル ----------

bool shell_exit = false;

void processCommand(const String &cmd)
{
  if (cmd == "help") {
    Serial.println("Commands:");
    Serial.println("  time                        - show current RTC time");
    Serial.println("  time set YYYY-MM-DD HH:MM:SS - set RTC time");
    Serial.println("  schedule                    - show unlock/lock schedule");
    Serial.println("  unlock HH:MM                - set daily unlock time");
    Serial.println("  lock HH:MM                  - set daily lock time");
    Serial.println("  reset                       - reset today's flags");
    Serial.println("  run                         - exit shell and start operation");
  }
  else if (cmd == "time") {
    RtcDateTime now = Rtc.GetDateTime();
    Serial.printf("%04u-%02u-%02u %02u:%02u:%02u\n",
      now.Year(), now.Month(), now.Day(),
      now.Hour(), now.Minute(), now.Second());
  }
  else if (cmd.startsWith("time set ")) {
    int y, mo, d, h, m, s;
    if (sscanf(cmd.c_str() + 9, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &m, &s) == 6) {
      Rtc.SetDateTime(RtcDateTime(y, mo, d, h, m, s));
      Serial.printf("RTC set: %04d-%02d-%02d %02d:%02d:%02d\n", y, mo, d, h, m, s);
    } else {
      Serial.println("Error: format is YYYY-MM-DD HH:MM:SS");
    }
  }
  else if (cmd == "schedule") {
    Serial.printf("Unlock: %02u:%02u\n", unlock_hour, unlock_min);
    Serial.printf("Lock:   %02u:%02u\n", lock_hour,   lock_min);
  }
  else if (cmd.startsWith("unlock ")) {
    int h, m;
    if (sscanf(cmd.c_str() + 7, "%d:%d", &h, &m) == 2 && h < 24 && m < 60) {
      unlock_hour = h; unlock_min = m;
      saveSchedule();
      Serial.printf("Unlock set to %02d:%02d\n", h, m);
    } else {
      Serial.println("Error: format is HH:MM");
    }
  }
  else if (cmd.startsWith("lock ")) {
    int h, m;
    if (sscanf(cmd.c_str() + 5, "%d:%d", &h, &m) == 2 && h < 24 && m < 60) {
      lock_hour = h; lock_min = m;
      saveSchedule();
      Serial.printf("Lock set to %02d:%02d\n", h, m);
    } else {
      Serial.println("Error: format is HH:MM");
    }
  }
  else if (cmd == "reset") {
    unlocked_today = false;
    locked_today = false;
    Serial.println("Today's flags reset.");
  }
  else if (cmd == "run") {
    shell_exit = true;
    Serial.println("Starting normal operation...");
  }
  else {
    Serial.printf("Unknown: %s\n", cmd.c_str());
  }
}

void runShell()
{
  Serial.println("\n=== Sesami Shell ===");
  Serial.println("Type 'help' for commands. Auto-starts in 10s.\n");

  String line = "";
  unsigned long timeout = millis() + 10'000;

  Serial.print("> ");
  while (!shell_exit && millis() < timeout) {
    while (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        Serial.println();
        if (line.length() > 0) {
          processCommand(line);
          line = "";
          if (!shell_exit) {
            timeout = millis() + 10'000;
            Serial.print("> ");
          }
        }
      } else if (c >= 32) {
        Serial.print(c); // echo
        line += c;
      }
    }
  }
  if (!shell_exit)
    Serial.println("\nTimeout. Starting normal operation...");
}

// ---------- スケジュール管理 ----------

void sleepUntilNextEvent()
{
  RtcDateTime now = Rtc.GetDateTime();
  int32_t cur     = now.Hour() * 3600 + now.Minute() * 60 + now.Second();
  int32_t unlock_t = unlock_hour * 3600 + unlock_min * 60;
  int32_t lock_t   = lock_hour   * 3600 + lock_min   * 60;

  int32_t next;
  if (!unlocked_today)       next = unlock_t;
  else if (!locked_today)    next = lock_t;
  else                       next = unlock_t + 86400; // 翌日unlock

  int32_t sleep_sec = next - cur;
  if (sleep_sec <= 0) sleep_sec += 86400;
  if (sleep_sec > 3600) sleep_sec = 3600; // 最大1時間ずつ

  Serial.printf("Next event in %d sec. Sleeping.\n", sleep_sec);
  esp_sleep_enable_timer_wakeup((uint64_t)sleep_sec * 1000'000ULL);
  esp_deep_sleep_start();
}

void setup()
{
  bool is_fresh_boot = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED);

  if (is_fresh_boot)
    delay(5'000);

  Serial.begin(115200);

#ifdef ARDUINO_M5Stick_C
  pinMode(10, OUTPUT);
  digitalWrite(10, 0);
#endif

  initRtc();
  loadSchedule();

  if (is_fresh_boot)
    runShell();

  // --- 通常オペレーション ---
  RtcDateTime now = Rtc.GetDateTime();
  Serial.printf("RTC: %04u-%02u-%02u %02u:%02u:%02u\n",
    now.Year(), now.Month(), now.Day(),
    now.Hour(), now.Minute(), now.Second());

  // 日付が変わったら今日のフラグをリセット
  if (now.Day() != last_action_day) {
    unlocked_today = false;
    locked_today = false;
    last_action_day = now.Day();
  }

  int32_t cur      = now.Hour() * 3600 + now.Minute() * 60 + now.Second();
  int32_t unlock_t = unlock_hour * 3600 + unlock_min * 60;
  int32_t lock_t   = lock_hour   * 3600 + lock_min   * 60;

  bool weekend = isWeekend(now);
  if (weekend)
    Serial.println("Weekend (JST): auto-unlock skipped.");

  bool should_unlock = !unlocked_today && cur >= unlock_t && !weekend;
  bool should_lock   = !locked_today   && cur >= lock_t;

  // 両方期限切れの場合はlockを優先
  if (should_lock)        action_lock   = true;
  else if (should_unlock) action_unlock = true;
  else                    sleepUntilNextEvent();

  BLEDevice::init("");
  scan_and_init();
  client.set_state_callback(state_update);

  Serial.print("Connecting...");
  connected = client.connect(3);
  Serial.println(connected ? "done" : "failed");
}

void loop()
{
  if (!connected) {
    delay(1000);
    return;
  }
  if (sesame_state == SesameClient::state_t::idle) {
    Serial.println("Failed to operate");
    connected = false;
    sleepUntilNextEvent();
  }
  if (sesame_state != SesameClient::state_t::active) {
    delay(100);
    return;
  }

  if (action_unlock) {
    Serial.println("Unlocking");
    bool ok = client.unlock("Auto Unlock");
    Serial.printf("unlock result=%d\n", ok);
    if (ok) unlocked_today = true;
  }
  else if (action_lock) {
    Serial.println("Locking");
    bool ok = client.lock("Auto Lock");
    Serial.printf("lock result=%d\n", ok);
    if (ok) locked_today = true;
  }

  delay(3000);
  client.disconnect();
  Serial.println("Disconnected");
  action_unlock = false;
  action_lock = false;
  connected = false;

  sleepUntilNextEvent();
}
