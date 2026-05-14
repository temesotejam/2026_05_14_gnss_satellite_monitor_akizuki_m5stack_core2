#include <Arduino.h>
#include <M5Unified.h>

#include <math.h>
#include <string.h>

// =============================
// Target configuration
// =============================
// Enable only one target at a time.
// Core2 + Akizuki GT-505GGBL5-DR-N:
#define USE_CORE2_AKIZUKI_GNSS
// CoreS3 + M5Stack GNSS Module / u-blox NEO-M9N:
// #define USE_CORES3_M5STACK_GNSS

#if defined(USE_CORE2_AKIZUKI_GNSS) && defined(USE_CORES3_M5STACK_GNSS)
#error "Enable only one target macro."
#endif

#if !defined(USE_CORE2_AKIZUKI_GNSS) && !defined(USE_CORES3_M5STACK_GNSS)
#error "Enable one target macro."
#endif

#if defined(USE_CORE2_AKIZUKI_GNSS)
// Core2 + Akizuki GT-505GGBL5-DR-N
// Adjust these pins if your Core2 UART wiring differs.
// The previous working project used 115200 bps, so keep that here.
static constexpr const char* kBoardLabel = "Core2";
static constexpr const char* kModuleLabel = "Akizuki GT-505";
static constexpr uint32_t kGnssBaudrate = 115200;
static constexpr int kGnssRxPin = 13;
static constexpr int kGnssTxPin = 14;
#endif

#if defined(USE_CORES3_M5STACK_GNSS)
// CoreS3 + M5Stack GNSS Module / u-blox NEO-M9N
// Existing project wiring uses Port.C UART RX=18, TX=17.
static constexpr const char* kBoardLabel = "CoreS3";
static constexpr const char* kModuleLabel = "M5 GNSS NEO-M9N";
static constexpr uint32_t kGnssBaudrate = 38400;
static constexpr int kGnssRxPin = 18;
static constexpr int kGnssTxPin = 17;
#endif

namespace {

// GGA gives fix state, position, used satellite count, HDOP, altitude.
// GSA gives the satellite IDs used for the current fix plus PDOP/HDOP/VDOP.
// GSV gives the visible satellites with C/N0, elevation, and azimuth.
// "Visible satellites" and "used satellites" are different concepts.

static constexpr size_t kMaxLineLength = 127;
static constexpr size_t kMaxFields = 24;
static constexpr size_t kMaxSatellites = 64;
static constexpr size_t kMaxUsedSatellites = 24;
static constexpr uint32_t kDisplayIntervalMs = 1000;
static constexpr uint32_t kSerialIntervalMs = 1000;
static constexpr uint32_t kNoDataTimeoutMs = 5000;
static constexpr uint32_t kSatelliteKeepaliveMs = 5000;
static constexpr uint32_t kGsaResetGapMs = 1500;

struct SatelliteRef {
  char system[6] = "GNSS";
  uint16_t id = 0;
};

struct SatelliteInfo {
  bool active = false;
  bool used = false;
  char system[6] = "GNSS";
  uint16_t id = 0;
  int16_t elevation_deg = -1;
  int16_t azimuth_deg = -1;
  int16_t cno_dbhz = -1;
  uint32_t last_seen_ms = 0;
  uint16_t cycle_id = 0;
};

struct GsvSystemState {
  char system[6] = "GNSS";
  uint16_t active_cycle = 0;
  uint16_t reported_visible = 0;
  uint32_t last_update_ms = 0;
};

struct GnssStatus {
  bool has_gga = false;
  bool has_rmc = false;
  bool has_location = false;
  bool has_altitude = false;
  bool has_gga_hdop = false;
  bool has_pdop = false;
  bool has_gsa_hdop = false;
  bool has_vdop = false;
  bool nmea_received = false;

  uint8_t gga_fix_quality = 0;
  uint8_t gsa_fix_type = 1;
  uint8_t gga_used_satellites = 0;
  uint8_t gsa_used_satellites = 0;
  uint16_t visible_satellites = 0;
  uint16_t gsv_reported_visible = 0;

  double latitude_deg = 0.0;
  double longitude_deg = 0.0;
  float altitude_m = 0.0f;
  float pdop = 0.0f;
  float hdop_gga = 0.0f;
  float hdop_gsa = 0.0f;
  float vdop = 0.0f;
  float speed_kmh = 0.0f;
  bool has_speed = false;

  uint32_t chars_processed = 0;
  uint32_t last_sentence_ms = 0;
};

HardwareSerial& gnssSerial = Serial2;
GnssStatus gnss;
SatelliteInfo satellites[kMaxSatellites];
SatelliteRef used_satellites[kMaxUsedSatellites];
size_t used_satellite_count = 0;
GsvSystemState gsv_states[8];
size_t gsv_state_count = 0;

char line_buffer[kMaxLineLength + 1] = {};
size_t line_length = 0;
uint32_t boot_ms = 0;
uint32_t last_display_ms = 0;
uint32_t last_serial_ms = 0;
uint32_t last_gsa_sentence_ms = 0;
uint16_t next_gsv_cycle_id = 1;
bool no_data_warning_reported = false;

uint8_t hexToNibble(char c) {
  if (c >= '0' && c <= '9') {
    return static_cast<uint8_t>(c - '0');
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<uint8_t>(c - 'A' + 10);
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<uint8_t>(c - 'a' + 10);
  }
  return 0xFF;
}

bool isFieldPresent(const char* text) {
  return text != nullptr && text[0] != '\0';
}

float parseFloatOr(const char* text, float fallback) {
  if (!isFieldPresent(text)) {
    return fallback;
  }
  return static_cast<float>(strtod(text, nullptr));
}

uint16_t parseUint16Or(const char* text, uint16_t fallback) {
  if (!isFieldPresent(text)) {
    return fallback;
  }
  return static_cast<uint16_t>(strtoul(text, nullptr, 10));
}

double parseLatLon(const char* field, const char* hemi) {
  if (!isFieldPresent(field) || !isFieldPresent(hemi)) {
    return 0.0;
  }

  const double raw = strtod(field, nullptr);
  const double degrees = floor(raw / 100.0);
  const double minutes = raw - (degrees * 100.0);
  double value = degrees + (minutes / 60.0);

  if (hemi[0] == 'S' || hemi[0] == 'W') {
    value = -value;
  }
  return value;
}

bool verifyChecksum(const char* line) {
  const char* star = strchr(line, '*');
  if (line[0] != '$' || star == nullptr || (star - line) < 1) {
    return false;
  }
  if (star[1] == '\0' || star[2] == '\0') {
    return false;
  }

  uint8_t checksum = 0;
  for (const char* p = line + 1; p < star; ++p) {
    checksum ^= static_cast<uint8_t>(*p);
  }

  const uint8_t high = hexToNibble(star[1]);
  const uint8_t low = hexToNibble(star[2]);
  if (high == 0xFF || low == 0xFF) {
    return false;
  }
  return checksum == static_cast<uint8_t>((high << 4) | low);
}

size_t splitFields(char* payload, char* fields[], size_t max_fields) {
  size_t count = 0;
  char* cursor = payload;

  while (count < max_fields && cursor != nullptr) {
    fields[count++] = cursor;
    char* comma = strchr(cursor, ',');
    if (comma == nullptr) {
      break;
    }
    *comma = '\0';
    cursor = comma + 1;
  }
  return count;
}

const char* inferSystemFromId(uint16_t id) {
  if (id >= 193 && id <= 199) {
    return "QZSS";
  }
  if (id >= 65 && id <= 96) {
    return "GLO";
  }
  if (id >= 201 && id <= 237) {
    return "BDS";
  }
  if (id >= 301 && id <= 336) {
    return "GAL";
  }
  if (id >= 1 && id <= 32) {
    return "GPS";
  }
  return "GNSS";
}

const char* talkerToSystem(const char* talker, uint16_t sat_id) {
  if (strcmp(talker, "GP") == 0) {
    return "GPS";
  }
  if (strcmp(talker, "GL") == 0) {
    return "GLO";
  }
  if (strcmp(talker, "GA") == 0) {
    return "GAL";
  }
  if (strcmp(talker, "GB") == 0 || strcmp(talker, "BD") == 0) {
    return "BDS";
  }
  if (strcmp(talker, "GQ") == 0) {
    return "QZSS";
  }
  return inferSystemFromId(sat_id);
}

const char* gsaSystemIdToSystem(uint16_t system_id) {
  switch (system_id) {
    case 1:
      return "GPS";
    case 2:
      return "GLO";
    case 3:
      return "GAL";
    case 4:
      return "BDS";
    default:
      return "GNSS";
  }
}

const char* fixTypeLabel(uint8_t fix_type) {
  switch (fix_type) {
    case 3:
      return "3D Fix";
    case 2:
      return "2D Fix";
    case 1:
    default:
      return "No Fix";
  }
}

void clearUsedSatellites() {
  used_satellite_count = 0;
  for (size_t i = 0; i < kMaxSatellites; ++i) {
    satellites[i].used = false;
  }
}

bool satelliteRefEquals(const SatelliteRef& ref, const char* system, uint16_t id) {
  return ref.id == id && strcmp(ref.system, system) == 0;
}

bool isSatelliteUsed(const char* system, uint16_t id) {
  for (size_t i = 0; i < used_satellite_count; ++i) {
    if (satelliteRefEquals(used_satellites[i], system, id)) {
      return true;
    }
  }
  return false;
}

void addUsedSatellite(const char* system, uint16_t id) {
  if (id == 0) {
    return;
  }

  for (size_t i = 0; i < used_satellite_count; ++i) {
    if (satelliteRefEquals(used_satellites[i], system, id)) {
      return;
    }
  }

  if (used_satellite_count >= kMaxUsedSatellites) {
    return;
  }

  strncpy(used_satellites[used_satellite_count].system, system,
          sizeof(used_satellites[used_satellite_count].system) - 1);
  used_satellites[used_satellite_count].system[sizeof(used_satellites[used_satellite_count].system) - 1] =
      '\0';
  used_satellites[used_satellite_count].id = id;
  ++used_satellite_count;
}

SatelliteInfo* findSatellite(const char* system, uint16_t id) {
  for (size_t i = 0; i < kMaxSatellites; ++i) {
    if (satellites[i].active && satellites[i].id == id &&
        strcmp(satellites[i].system, system) == 0) {
      return &satellites[i];
    }
  }

  for (size_t i = 0; i < kMaxSatellites; ++i) {
    if (!satellites[i].active) {
      satellites[i].active = true;
      satellites[i].id = id;
      strncpy(satellites[i].system, system, sizeof(satellites[i].system) - 1);
      satellites[i].system[sizeof(satellites[i].system) - 1] = '\0';
      satellites[i].used = false;
      satellites[i].elevation_deg = -1;
      satellites[i].azimuth_deg = -1;
      satellites[i].cno_dbhz = -1;
      satellites[i].last_seen_ms = 0;
      satellites[i].cycle_id = 0;
      return &satellites[i];
    }
  }

  return nullptr;
}

GsvSystemState* getGsvSystemState(const char* system) {
  for (size_t i = 0; i < gsv_state_count; ++i) {
    if (strcmp(gsv_states[i].system, system) == 0) {
      return &gsv_states[i];
    }
  }

  if (gsv_state_count >= (sizeof(gsv_states) / sizeof(gsv_states[0]))) {
    return nullptr;
  }

  strncpy(gsv_states[gsv_state_count].system, system,
          sizeof(gsv_states[gsv_state_count].system) - 1);
  gsv_states[gsv_state_count].system[sizeof(gsv_states[gsv_state_count].system) - 1] = '\0';
  gsv_states[gsv_state_count].active_cycle = 0;
  gsv_states[gsv_state_count].reported_visible = 0;
  gsv_states[gsv_state_count].last_update_ms = 0;
  ++gsv_state_count;
  return &gsv_states[gsv_state_count - 1];
}

void refreshSatelliteUsageFlags() {
  const uint32_t now = millis();
  uint16_t visible_count = 0;
  uint16_t gsv_visible_total = 0;

  for (size_t i = 0; i < kMaxSatellites; ++i) {
    if (!satellites[i].active) {
      continue;
    }

    if (now - satellites[i].last_seen_ms > kSatelliteKeepaliveMs) {
      satellites[i].active = false;
      satellites[i].used = false;
      continue;
    }

    satellites[i].used = isSatelliteUsed(satellites[i].system, satellites[i].id);
    ++visible_count;
  }

  for (size_t i = 0; i < gsv_state_count; ++i) {
    if (now - gsv_states[i].last_update_ms <= kSatelliteKeepaliveMs) {
      gsv_visible_total = static_cast<uint16_t>(gsv_visible_total + gsv_states[i].reported_visible);
    }
  }

  gnss.gsa_used_satellites = static_cast<uint8_t>(used_satellite_count);
  gnss.gsv_reported_visible = gsv_visible_total;
  gnss.visible_satellites = gsv_visible_total > visible_count ? gsv_visible_total : visible_count;
}

void parseGga(char* fields[], size_t field_count) {
  if (field_count < 10) {
    return;
  }

  gnss.has_gga = true;
  gnss.gga_fix_quality = static_cast<uint8_t>(parseUint16Or(fields[6], 0));
  gnss.gga_used_satellites = static_cast<uint8_t>(parseUint16Or(fields[7], 0));
  gnss.hdop_gga = parseFloatOr(fields[8], gnss.hdop_gga);
  gnss.has_gga_hdop = isFieldPresent(fields[8]);
  gnss.altitude_m = parseFloatOr(fields[9], gnss.altitude_m);
  gnss.has_altitude = isFieldPresent(fields[9]);

  if (gnss.gga_fix_quality > 0 && isFieldPresent(fields[2]) && isFieldPresent(fields[3]) &&
      isFieldPresent(fields[4]) && isFieldPresent(fields[5])) {
    gnss.latitude_deg = parseLatLon(fields[2], fields[3]);
    gnss.longitude_deg = parseLatLon(fields[4], fields[5]);
    gnss.has_location = true;
  }
}

void parseRmc(char* fields[], size_t field_count) {
  if (field_count < 8) {
    return;
  }

  gnss.has_rmc = true;
  const bool active = isFieldPresent(fields[2]) && fields[2][0] == 'A';

  if (active && isFieldPresent(fields[3]) && isFieldPresent(fields[4]) &&
      isFieldPresent(fields[5]) && isFieldPresent(fields[6])) {
    gnss.latitude_deg = parseLatLon(fields[3], fields[4]);
    gnss.longitude_deg = parseLatLon(fields[5], fields[6]);
    gnss.has_location = true;
  }

  if (active && isFieldPresent(fields[7])) {
    gnss.speed_kmh = parseFloatOr(fields[7], 0.0f) * 1.852f;
    gnss.has_speed = true;
  }
}

void parseGsa(const char* talker, char* fields[], size_t field_count) {
  if (field_count < 17) {
    return;
  }

  const uint32_t now = millis();
  if (now - last_gsa_sentence_ms > kGsaResetGapMs) {
    clearUsedSatellites();
  }
  last_gsa_sentence_ms = now;

  gnss.gsa_fix_type = static_cast<uint8_t>(parseUint16Or(fields[2], gnss.gsa_fix_type));

  const uint16_t system_id = field_count > 18 ? parseUint16Or(fields[18], 0) : 0;
  const char* gsa_system =
      system_id != 0 ? gsaSystemIdToSystem(system_id) : talkerToSystem(talker, 0);

  for (size_t i = 3; i <= 14 && i < field_count; ++i) {
    const uint16_t sat_id = parseUint16Or(fields[i], 0);
    if (sat_id == 0) {
      continue;
    }
    addUsedSatellite(gsa_system, sat_id);
  }

  gnss.pdop = parseFloatOr(fields[15], gnss.pdop);
  gnss.hdop_gsa = parseFloatOr(fields[16], gnss.hdop_gsa);
  gnss.vdop = (field_count > 17) ? parseFloatOr(fields[17], gnss.vdop) : gnss.vdop;
  gnss.has_pdop = isFieldPresent(fields[15]);
  gnss.has_gsa_hdop = isFieldPresent(fields[16]);
  gnss.has_vdop = (field_count > 17) && isFieldPresent(fields[17]);

  refreshSatelliteUsageFlags();
}

void parseGsv(const char* talker, char* fields[], size_t field_count) {
  if (field_count < 4) {
    return;
  }

  const uint16_t total_messages = parseUint16Or(fields[1], 0);
  const uint16_t message_number = parseUint16Or(fields[2], 0);
  const uint16_t reported_visible = parseUint16Or(fields[3], 0);
  const char* state_system = talkerToSystem(talker, 0);
  GsvSystemState* state = getGsvSystemState(state_system);
  if (state == nullptr) {
    return;
  }

  if (message_number <= 1 || state->active_cycle == 0) {
    state->active_cycle = next_gsv_cycle_id++;
  }
  state->reported_visible = reported_visible;
  state->last_update_ms = millis();

  for (size_t i = 4; i + 3 < field_count; i += 4) {
    const uint16_t sat_id = parseUint16Or(fields[i], 0);
    if (sat_id == 0) {
      continue;
    }

    const char* system = talkerToSystem(talker, sat_id);
    SatelliteInfo* sat = findSatellite(system, sat_id);
    if (sat == nullptr) {
      continue;
    }

    sat->active = true;
    sat->id = sat_id;
    sat->elevation_deg = static_cast<int16_t>(parseUint16Or(fields[i + 1], 0));
    sat->azimuth_deg = static_cast<int16_t>(parseUint16Or(fields[i + 2], 0));
    sat->cno_dbhz = isFieldPresent(fields[i + 3])
                        ? static_cast<int16_t>(parseUint16Or(fields[i + 3], 0))
                        : -1;
    sat->last_seen_ms = millis();
    sat->cycle_id = state->active_cycle;
    sat->used = isSatelliteUsed(system, sat_id);
  }

  if (message_number == total_messages) {
    for (size_t i = 0; i < kMaxSatellites; ++i) {
      if (!satellites[i].active) {
        continue;
      }
      if (strcmp(satellites[i].system, state_system) != 0) {
        continue;
      }
      if (satellites[i].cycle_id != state->active_cycle) {
        satellites[i].active = false;
        satellites[i].used = false;
      }
    }
  }

  refreshSatelliteUsageFlags();
}

void processLine(const char* line) {
  if (line[0] != '$' || !verifyChecksum(line)) {
    return;
  }

  char payload[kMaxLineLength + 1] = {};
  const char* star = strchr(line, '*');
  if (star == nullptr) {
    return;
  }

  const size_t payload_length = static_cast<size_t>(star - (line + 1));
  if (payload_length == 0 || payload_length > kMaxLineLength) {
    return;
  }

  memcpy(payload, line + 1, payload_length);
  payload[payload_length] = '\0';

  char* fields[kMaxFields] = {};
  const size_t field_count = splitFields(payload, fields, kMaxFields);
  if (field_count == 0 || strlen(fields[0]) < 5) {
    return;
  }

  char talker[3] = {fields[0][0], fields[0][1], '\0'};
  const char* sentence = fields[0] + 2;

  gnss.nmea_received = true;
  gnss.last_sentence_ms = millis();

  if (strcmp(sentence, "GGA") == 0) {
    parseGga(fields, field_count);
  } else if (strcmp(sentence, "GSA") == 0) {
    parseGsa(talker, fields, field_count);
  } else if (strcmp(sentence, "GSV") == 0) {
    parseGsv(talker, fields, field_count);
  } else if (strcmp(sentence, "RMC") == 0) {
    parseRmc(fields, field_count);
  }
}

void readGnssStream() {
  while (gnssSerial.available() > 0) {
    const int raw = gnssSerial.read();
    if (raw < 0) {
      break;
    }

    ++gnss.chars_processed;
    const char c = static_cast<char>(raw);

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      if (line_length > 0) {
        line_buffer[line_length] = '\0';
        processLine(line_buffer);
        line_length = 0;
      }
      continue;
    }

    if (c == '$') {
      line_length = 0;
    }

    if (line_length < kMaxLineLength) {
      line_buffer[line_length++] = c;
    } else {
      line_length = 0;
    }
  }
}

size_t buildSortedSatelliteIndex(size_t indices[], size_t max_indices) {
  const uint32_t now = millis();
  size_t count = 0;

  for (size_t i = 0; i < kMaxSatellites && count < max_indices; ++i) {
    if (!satellites[i].active) {
      continue;
    }
    if (now - satellites[i].last_seen_ms > kSatelliteKeepaliveMs) {
      continue;
    }
    indices[count++] = i;
  }

  for (size_t i = 0; i < count; ++i) {
    size_t best = i;
    for (size_t j = i + 1; j < count; ++j) {
      const SatelliteInfo& a = satellites[indices[best]];
      const SatelliteInfo& b = satellites[indices[j]];

      const int a_used = a.used ? 1 : 0;
      const int b_used = b.used ? 1 : 0;
      const int a_cno = a.cno_dbhz >= 0 ? a.cno_dbhz : -1;
      const int b_cno = b.cno_dbhz >= 0 ? b.cno_dbhz : -1;

      if (b_used > a_used || (b_used == a_used && b_cno > a_cno) ||
          (b_used == a_used && b_cno == a_cno && b.id < a.id)) {
        best = j;
      }
    }
    if (best != i) {
      const size_t tmp = indices[i];
      indices[i] = indices[best];
      indices[best] = tmp;
    }
  }

  return count;
}

void drawFieldLine(const char* label, const char* value) {
  M5.Display.printf("%-8s %s\n", label, value);
}

void drawDoubleField(const char* label, bool valid, double value, uint8_t decimals, const char* suffix = "") {
  if (valid) {
    M5.Display.printf("%-8s %.*f%s\n", label, decimals, value, suffix);
  } else {
    M5.Display.printf("%-8s --\n", label);
  }
}

void drawIntField(const char* label, bool valid, int value, const char* suffix = "") {
  if (valid) {
    M5.Display.printf("%-8s %d%s\n", label, value, suffix);
  } else {
    M5.Display.printf("%-8s --\n", label);
  }
}

void drawScreen() {
  const bool show_no_data_warning = !gnss.nmea_received && (millis() - boot_ms >= kNoDataTimeoutMs);
  const size_t max_rows = 7;
  size_t indices[kMaxSatellites] = {};
  const size_t visible = buildSortedSatelliteIndex(indices, kMaxSatellites);

  M5.Display.startWrite();
  M5.Display.fillScreen(BLACK);

  const uint16_t banner_bg = show_no_data_warning
                                 ? RED
                                 : (gnss.gsa_fix_type >= 3 ? GREEN : (gnss.gsa_fix_type == 2 ? YELLOW : DARKGREY));
  const uint16_t banner_fg = (banner_bg == GREEN || banner_bg == YELLOW) ? BLACK : WHITE;

  M5.Display.fillRect(0, 0, M5.Display.width(), 34, banner_bg);
  M5.Display.setCursor(8, 6);
  M5.Display.setTextColor(banner_fg, banner_bg);
  M5.Display.setTextSize(2);
  M5.Display.printf("%s / %s\n", kBoardLabel, fixTypeLabel(gnss.gsa_fix_type));

  M5.Display.setCursor(8, 40);
  M5.Display.setTextColor(WHITE, BLACK);
  M5.Display.setTextSize(1);
  drawFieldLine("Module", kModuleLabel);
  drawDoubleField("Lat", gnss.has_location, gnss.latitude_deg, 6);
  drawDoubleField("Lon", gnss.has_location, gnss.longitude_deg, 6);
  drawDoubleField("Alt", gnss.has_altitude, gnss.altitude_m, 1, " m");
  drawIntField("UsedGGA", true, gnss.gga_used_satellites);
  drawIntField("UsedGSA", true, gnss.gsa_used_satellites);
  drawIntField("Visible", true, gnss.visible_satellites);
  drawIntField("GSVSeen", true, gnss.gsv_reported_visible);
  drawDoubleField("HDOP", gnss.has_gga_hdop || gnss.has_gsa_hdop,
                  gnss.has_gga_hdop ? gnss.hdop_gga : gnss.hdop_gsa, 2);
  drawDoubleField("PDOP", gnss.has_pdop, gnss.pdop, 2);
  drawDoubleField("VDOP", gnss.has_vdop, gnss.vdop, 2);

  if (show_no_data_warning) {
    M5.Display.println("No GNSS data. Check UART, module, antenna.");
  }

  M5.Display.println();
  M5.Display.println("[Satellites]");
  M5.Display.println("SYS  ID  USE  CNO  EL  AZ");

  const size_t rows = visible < max_rows ? visible : max_rows;
  for (size_t i = 0; i < rows; ++i) {
    const SatelliteInfo& sat = satellites[indices[i]];
    M5.Display.printf("%-4s %3u   %c  %3d %3d %3d\n", sat.system, sat.id,
                      sat.used ? '*' : '-', sat.cno_dbhz, sat.elevation_deg,
                      sat.azimuth_deg);
  }

  if (visible > rows) {
    M5.Display.printf("... %u more satellites\n", static_cast<unsigned>(visible - rows));
  }

  M5.Display.endWrite();
}

void printSerialReport() {
  const size_t max_rows = 12;
  size_t indices[kMaxSatellites] = {};
  const size_t visible = buildSortedSatelliteIndex(indices, kMaxSatellites);

  Serial.println();
  Serial.println("===== GNSS STATUS =====");
  Serial.printf("Board: %s\n", kBoardLabel);
  Serial.printf("Module: %s\n", kModuleLabel);
  Serial.printf("Fix: %s\n", fixTypeLabel(gnss.gsa_fix_type));

  if (gnss.has_location) {
    Serial.printf("Lat: %.6f\n", gnss.latitude_deg);
    Serial.printf("Lon: %.6f\n", gnss.longitude_deg);
  } else {
    Serial.println("Lat: --");
    Serial.println("Lon: --");
  }

  if (gnss.has_altitude) {
    Serial.printf("Alt: %.1f m\n", gnss.altitude_m);
  } else {
    Serial.println("Alt: --");
  }

  Serial.printf("Used satellites (GGA): %u\n", gnss.gga_used_satellites);
  Serial.printf("Used satellites (GSA count): %u\n", gnss.gsa_used_satellites);
  Serial.printf("Visible satellites (merged): %u\n", gnss.visible_satellites);
  Serial.printf("Visible satellites (GSV reported): %u\n", gnss.gsv_reported_visible);
  if (gnss.has_gga_hdop || gnss.has_gsa_hdop) {
    Serial.printf("HDOP: %.2f\n", gnss.has_gga_hdop ? gnss.hdop_gga : gnss.hdop_gsa);
  } else {
    Serial.println("HDOP: --");
  }
  if (gnss.has_pdop) {
    Serial.printf("PDOP: %.2f\n", gnss.pdop);
  } else {
    Serial.println("PDOP: --");
  }
  if (gnss.has_vdop) {
    Serial.printf("VDOP: %.2f\n", gnss.vdop);
  } else {
    Serial.println("VDOP: --");
  }
  Serial.printf("Chars processed: %lu\n", static_cast<unsigned long>(gnss.chars_processed));
  Serial.println("SYS,ID,USED,CNO,ELEV,AZIM");

  const size_t rows = visible < max_rows ? visible : max_rows;
  for (size_t i = 0; i < rows; ++i) {
    const SatelliteInfo& sat = satellites[indices[i]];
    Serial.printf("%s,%u,%u,%d,%d,%d\n", sat.system, sat.id, sat.used ? 1 : 0,
                  sat.cno_dbhz, sat.elevation_deg, sat.azimuth_deg);
  }
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setTextWrap(false);
  M5.Display.setTextSize(1);

  Serial.begin(115200);
  gnssSerial.begin(kGnssBaudrate, SERIAL_8N1, kGnssRxPin, kGnssTxPin);

  boot_ms = millis();

  M5.Display.fillScreen(BLACK);
  M5.Display.setCursor(8, 8);
  M5.Display.setTextColor(WHITE, BLACK);
  M5.Display.setTextSize(2);
  M5.Display.println("GNSS Monitor");
  M5.Display.setTextSize(1);
  M5.Display.printf("%s / %s\n", kBoardLabel, kModuleLabel);
  M5.Display.printf("UART RX=%d TX=%d @ %lu\n", kGnssRxPin, kGnssTxPin,
                    static_cast<unsigned long>(kGnssBaudrate));
  M5.Display.println("Parsing GGA/GSA/GSV/RMC...");

  Serial.println("GNSS monitor started");
  Serial.printf("Board: %s\n", kBoardLabel);
  Serial.printf("Module: %s\n", kModuleLabel);
  Serial.printf("UART: Serial2.begin(%lu, SERIAL_8N1, %d, %d)\n",
                static_cast<unsigned long>(kGnssBaudrate), kGnssRxPin, kGnssTxPin);
}

void loop() {
  M5.update();
  readGnssStream();
  refreshSatelliteUsageFlags();

  const uint32_t now = millis();
  const bool show_no_data_warning = !gnss.nmea_received && (now - boot_ms >= kNoDataTimeoutMs);
  if (show_no_data_warning && !no_data_warning_reported) {
    no_data_warning_reported = true;
    Serial.println("No GNSS data received. Check module, UART pins, baudrate, and antenna.");
  }

  if (now - last_display_ms >= kDisplayIntervalMs) {
    drawScreen();
    last_display_ms = now;
  }

  if (now - last_serial_ms >= kSerialIntervalMs) {
    printSerialReport();
    last_serial_ms = now;
  }
}
