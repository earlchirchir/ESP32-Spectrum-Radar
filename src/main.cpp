#include <Arduino.h>
#include <esp32_smartdisplay.h>
#include <WiFi.h>

#define MAX_NETWORKS 35
#define WATERFALL_ROWS 8
#define WATERFALL_COLS 13

struct WiFiItem {
    char ssid[33];
    int32_t rssi;
    uint8_t channel;
    char bssidStr[18];
    wifi_auth_mode_t authmode;
};

static WiFiItem wifiList[MAX_NETWORKS];
static uint16_t wifiCount = 0;
static uint8_t channelApCount[14]; // Channels 1-13
static int32_t channelPeakRssi[14]; // Strongest RSSI per channel
static char channelTopSsid[14][33];  // Strongest SSID per channel
static SemaphoreHandle_t dataMutex = NULL;

// Rolling Waterfall History (Rows: 0=Newest, 7=Oldest)
static uint8_t waterfallHistory[WATERFALL_ROWS][WATERFALL_COLS];
static lv_obj_t* waterfall_cells[WATERFALL_ROWS][WATERFALL_COLS];

// LVGL UI Widgets
static lv_obj_t* tabview = nullptr;
static lv_obj_t* tab_spectrum = nullptr;
static lv_obj_t* tab_devices = nullptr;
static lv_obj_t* tab_waterfall = nullptr;
static lv_obj_t* spectrum_chart = nullptr;

// 3 Color Series for Bars: Green (Low), Yellow (Med), Red (Heavy)
static lv_chart_series_t* series_green = nullptr;
static lv_chart_series_t* series_yellow = nullptr;
static lv_chart_series_t* series_red = nullptr;

static lv_obj_t* device_list = nullptr;
static lv_obj_t* status_label = nullptr;
static lv_obj_t* channel_val_labels[14]; // AP Count labels above bars
static lv_obj_t* summary_label = nullptr;
static lv_obj_t* inspector_modal = nullptr;

// Helper: Convert Auth Mode Enum to Human Readable String
const char* getAuthModeName(wifi_auth_mode_t authmode) {
    switch (authmode) {
        case WIFI_AUTH_OPEN: return "Open (Unsecured)";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA-PSK";
        case WIFI_AUTH_WPA2_PSK: return "WPA2-PSK (AES)";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2-PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
        case WIFI_AUTH_WPA3_PSK: return "WPA3-PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3-PSK";
        default: return "Secured";
    }
}

// Close Inspector Modal Callback
static void close_inspector_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_RELEASED) {
        if (inspector_modal) {
            lv_obj_delete(inspector_modal);
            inspector_modal = nullptr;
        }
    }
}

// Open Inspector Modal Window
void openNetworkInspector(uint32_t idx) {
    if (idx >= wifiCount) return;
    if (inspector_modal) {
        lv_obj_delete(inspector_modal);
        inspector_modal = nullptr;
    }

    WiFiItem item = wifiList[idx];

    // Parent Modal Overlay Screen (300x210px)
    inspector_modal = lv_obj_create(lv_screen_active());
    lv_obj_set_size(inspector_modal, 300, 210);
    lv_obj_align(inspector_modal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(inspector_modal, lv_color_make(18, 24, 38), LV_OPA_COVER);
    lv_obj_set_style_border_color(inspector_modal, lv_color_make(0, 210, 255), LV_OPA_COVER);
    lv_obj_set_style_border_width(inspector_modal, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(inspector_modal, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(inspector_modal, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_all(inspector_modal, 10, LV_PART_MAIN);

    // Title: Network SSID Name
    lv_obj_t* title_lbl = lv_label_create(inspector_modal);
    char tBuf[40];
    snprintf(tBuf, sizeof(tBuf), "📶 %s", item.ssid);
    lv_label_set_text(title_lbl, tBuf);
    lv_obj_set_style_text_color(title_lbl, lv_color_make(0, 240, 255), LV_OPA_COVER);
    lv_obj_set_style_text_font(title_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 0, 2);

    // Top-Right X Close Icon Button
    lv_obj_t* x_btn = lv_button_create(inspector_modal);
    lv_obj_set_size(x_btn, 30, 26);
    lv_obj_align(x_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(x_btn, lv_color_make(180, 40, 40), LV_PART_MAIN);
    lv_obj_add_event_cb(x_btn, close_inspector_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* x_lbl = lv_label_create(x_btn);
    lv_label_set_text(x_lbl, "X");
    lv_obj_remove_flag(x_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(x_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(x_lbl, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_center(x_lbl);

    // Info Details Block
    lv_obj_t* info_lbl = lv_label_create(inspector_modal);
    char infoBuf[160];

    int qualityPct = constrain(map(item.rssi, -100, -30, 0, 100), 0, 100);
    const char* qualDesc = (qualityPct > 70) ? "Excellent" : (qualityPct > 40 ? "Good" : "Weak");

    float freqGHz = 2.407f + (item.channel * 0.005f);

    snprintf(infoBuf, sizeof(infoBuf),
             "BSSID: %s\n"
             "Channel: Ch %u (%.3f GHz)\n"
             "Security: %s\n"
             "Signal: %d dBm (%d%% - %s)",
             item.bssidStr[0] != '\0' ? item.bssidStr : "N/A",
             item.channel, freqGHz,
             getAuthModeName(item.authmode),
             item.rssi, qualityPct, qualDesc);

    lv_label_set_text(info_lbl, infoBuf);
    lv_obj_set_style_text_color(info_lbl, lv_color_make(220, 220, 230), LV_OPA_COVER);
    lv_obj_set_style_text_font(info_lbl, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_align(info_lbl, LV_ALIGN_TOP_LEFT, 0, 28);

    // Visual Signal Strength Bar
    lv_obj_t* bar = lv_bar_create(inspector_modal);
    lv_obj_set_size(bar, 276, 14);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 118);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, qualityPct, LV_ANIM_ON);

    // Color indicator bar based on quality
    if (qualityPct > 70) {
        lv_obj_set_style_bg_color(bar, lv_color_make(0, 230, 100), LV_PART_INDICATOR);
    } else if (qualityPct > 40) {
        lv_obj_set_style_bg_color(bar, lv_color_make(255, 200, 0), LV_PART_INDICATOR);
    } else {
        lv_obj_set_style_bg_color(bar, lv_color_make(255, 60, 60), LV_PART_INDICATOR);
    }

    // Prominent Large CLOSE Button (150x36px)
    lv_obj_t* close_btn = lv_button_create(inspector_modal);
    lv_obj_set_size(close_btn, 150, 36);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_make(0, 130, 240), LV_PART_MAIN);
    lv_obj_add_event_cb(close_btn, close_inspector_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn_lbl = lv_label_create(close_btn);
    lv_label_set_text(btn_lbl, "CLOSE INSPECTOR");
    lv_obj_remove_flag(btn_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn_lbl, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_center(btn_lbl);
}

// Device Button Click Handler
static void list_btn_event_cb(lv_event_t* e) {
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    openNetworkInspector(idx);
}

// Core 0 Network Scanner Task
void networkScannerTask(void* pvParameters) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    while (true) {
        int n = WiFi.scanNetworks(false, true); // Sync scan
        if (n >= 0) {
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                wifiCount = min(n, (int)MAX_NETWORKS);
                memset(channelApCount, 0, sizeof(channelApCount));
                for (int ch = 1; ch <= 13; ch++) {
                    channelPeakRssi[ch] = -120;
                    channelTopSsid[ch][0] = '\0';
                }

                for (int i = 0; i < wifiCount; i++) {
                    String s = WiFi.SSID(i);
                    if (s.length() == 0) s = "<Hidden>";
                    strncpy(wifiList[i].ssid, s.c_str(), sizeof(wifiList[i].ssid) - 1);
                    wifiList[i].ssid[sizeof(wifiList[i].ssid) - 1] = '\0';
                    wifiList[i].rssi = WiFi.RSSI(i);
                    wifiList[i].channel = WiFi.channel(i);
                    wifiList[i].authmode = WiFi.encryptionType(i);

                    String mac = WiFi.BSSIDstr(i);
                    strncpy(wifiList[i].bssidStr, mac.c_str(), sizeof(wifiList[i].bssidStr) - 1);
                    wifiList[i].bssidStr[sizeof(wifiList[i].bssidStr) - 1] = '\0';

                    if (wifiList[i].channel >= 1 && wifiList[i].channel <= 13) {
                        uint8_t ch = wifiList[i].channel;
                        channelApCount[ch]++;
                        if (wifiList[i].rssi > channelPeakRssi[ch]) {
                            channelPeakRssi[ch] = wifiList[i].rssi;
                            strncpy(channelTopSsid[ch], wifiList[i].ssid, sizeof(channelTopSsid[ch]) - 1);
                        }
                    }
                }
                xSemaphoreGive(dataMutex);
            }
            WiFi.scanDelete();
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void buildUI() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_screen_load(scr);
    lv_obj_set_style_bg_color(scr, lv_color_make(15, 18, 28), LV_OPA_COVER);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);

    // Header Title Bar (Screen Y = 1 to 15)
    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "ESP32 SPECTRUM RADAR | Scanning...");
    lv_obj_set_style_text_color(status_label, lv_color_make(0, 230, 255), LV_OPA_COVER);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 6, 1);

    // Tabview Setup (Full Screen Height = 224px, Y = 16 to 240)
    tabview = lv_tabview_create(scr);
    lv_obj_set_size(tabview, LV_PCT(100), 224);
    lv_obj_align(tabview, LV_ALIGN_BOTTOM_MID, 0, 0);

    // High-Contrast Menu Bar Styling (24px height)
    lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_height(tab_bar, 24);
    lv_obj_set_style_text_font(tab_bar, &lv_font_montserrat_12, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(tab_bar, lv_color_make(30, 42, 65), LV_PART_ITEMS);
    lv_obj_set_style_text_color(tab_bar, lv_color_make(180, 225, 255), LV_PART_ITEMS);

    lv_obj_set_style_bg_color(tab_bar, lv_color_make(0, 130, 240), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(tab_bar, lv_color_make(255, 255, 255), LV_PART_ITEMS | LV_STATE_CHECKED);

    tab_spectrum = lv_tabview_add_tab(tabview, "SPECTRUM");
    tab_devices = lv_tabview_add_tab(tabview, "DEVICES");
    tab_waterfall = lv_tabview_add_tab(tabview, "WATERFALL");

    // Remove internal tab container padding to maximize screen utilization
    lv_obj_set_style_pad_all(tab_spectrum, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab_devices, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab_waterfall, 0, LV_PART_MAIN);

    // -------------------------------------------------------------
    // Tab 1: Full-Screen Stretched 2.4GHz Spectrum Layout
    // -------------------------------------------------------------
    summary_label = lv_label_create(tab_spectrum);
    lv_label_set_text(summary_label, "2.4GHz Channels 1-13 | Analyzing Wi-Fi Congestion...");
    lv_obj_set_style_text_color(summary_label, lv_color_make(230, 230, 230), LV_OPA_COVER);
    lv_obj_set_style_text_font(summary_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(summary_label, LV_ALIGN_TOP_LEFT, 4, 0);

    lv_obj_t* y_axis_label = lv_label_create(tab_spectrum);
    lv_label_set_text(y_axis_label, "APs");
    lv_obj_set_style_text_color(y_axis_label, lv_color_make(0, 220, 255), LV_OPA_COVER);
    lv_obj_set_style_text_font(y_axis_label, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_align(y_axis_label, LV_ALIGN_TOP_LEFT, 0, 16);

    lv_obj_t* y10 = lv_label_create(tab_spectrum);
    lv_label_set_text(y10, "10-");
    lv_obj_set_style_text_color(y10, lv_color_make(150, 150, 170), LV_OPA_COVER);
    lv_obj_set_style_text_font(y10, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_align(y10, LV_ALIGN_TOP_LEFT, 0, 28);

    lv_obj_t* y5 = lv_label_create(tab_spectrum);
    lv_label_set_text(y5, " 5-");
    lv_obj_set_style_text_color(y5, lv_color_make(150, 150, 170), LV_OPA_COVER);
    lv_obj_set_style_text_font(y5, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_align(y5, LV_ALIGN_TOP_LEFT, 0, 88);

    lv_obj_t* y0 = lv_label_create(tab_spectrum);
    lv_label_set_text(y0, " 0-");
    lv_obj_set_style_text_color(y0, lv_color_make(150, 150, 170), LV_OPA_COVER);
    lv_obj_set_style_text_font(y0, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_align(y0, LV_ALIGN_TOP_LEFT, 0, 145);

    spectrum_chart = lv_chart_create(tab_spectrum);
    lv_obj_set_size(spectrum_chart, 275, 125);
    lv_obj_align(spectrum_chart, LV_ALIGN_TOP_LEFT, 24, 28);
    lv_chart_set_type(spectrum_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_point_count(spectrum_chart, 13);
    lv_chart_set_range(spectrum_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 10);
    lv_obj_set_style_bg_color(spectrum_chart, lv_color_make(18, 22, 36), LV_OPA_COVER);

    series_green = lv_chart_add_series(spectrum_chart, lv_color_make(0, 230, 100), LV_CHART_AXIS_PRIMARY_Y);
    series_yellow = lv_chart_add_series(spectrum_chart, lv_color_make(255, 200, 0), LV_CHART_AXIS_PRIMARY_Y);
    series_red = lv_chart_add_series(spectrum_chart, lv_color_make(255, 50, 50), LV_CHART_AXIS_PRIMARY_Y);

    for (int i = 0; i < 13; i++) {
        lv_chart_set_value_by_id(spectrum_chart, series_green, i, 0);
        lv_chart_set_value_by_id(spectrum_chart, series_yellow, i, 0);
        lv_chart_set_value_by_id(spectrum_chart, series_red, i, 0);
    }
    lv_chart_refresh(spectrum_chart);

    lv_obj_t* val_container = lv_obj_create(tab_spectrum);
    lv_obj_remove_style_all(val_container);
    lv_obj_set_size(val_container, 275, 12);
    lv_obj_align(val_container, LV_ALIGN_TOP_LEFT, 24, 16);
    lv_obj_set_flex_flow(val_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(val_container, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* x_container = lv_obj_create(tab_spectrum);
    lv_obj_remove_style_all(x_container);
    lv_obj_set_size(x_container, 275, 14);
    lv_obj_align(x_container, LV_ALIGN_TOP_LEFT, 24, 155);
    lv_obj_set_flex_flow(x_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(x_container, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int ch = 1; ch <= 13; ch++) {
        channel_val_labels[ch] = lv_label_create(val_container);
        lv_label_set_text(channel_val_labels[ch], "");
        lv_obj_set_style_text_font(channel_val_labels[ch], &lv_font_montserrat_10, LV_PART_MAIN);
        lv_obj_set_style_text_align(channel_val_labels[ch], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

        lv_obj_t* ch_lbl = lv_label_create(x_container);
        char chBuf[4];
        snprintf(chBuf, sizeof(chBuf), "%d", ch);
        lv_label_set_text(ch_lbl, chBuf);
        lv_obj_set_style_text_font(ch_lbl, &lv_font_montserrat_10, LV_PART_MAIN);
        lv_obj_set_style_text_align(ch_lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

        if (ch == 1 || ch == 6 || ch == 11) {
            lv_obj_set_style_text_color(ch_lbl, lv_color_make(255, 215, 0), LV_OPA_COVER);
        } else {
            lv_obj_set_style_text_color(ch_lbl, lv_color_make(170, 170, 190), LV_OPA_COVER);
        }
    }

    // Explicit Self-Explanatory Color Key Legend on Spectrum Tab
    lv_obj_t* legend_lbl = lv_label_create(tab_spectrum);
    lv_label_set_text(legend_lbl, "KEY: Green=Clean (1-2 APs)  Yellow=Med (3-5)  Red=Heavy (6+)");
    lv_obj_set_style_text_color(legend_lbl, lv_color_make(180, 200, 220), LV_OPA_COVER);
    lv_obj_set_style_text_font(legend_lbl, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_align(legend_lbl, LV_ALIGN_TOP_LEFT, 2, 172);

    // -------------------------------------------------------------
    // Tab 2: Discovered Wi-Fi Devices Explorer
    // -------------------------------------------------------------
    device_list = lv_list_create(tab_devices);
    lv_obj_set_size(device_list, LV_PCT(100), LV_PCT(100));
    lv_obj_align(device_list, LV_ALIGN_CENTER, 0, 0);
    lv_list_add_button(device_list, NULL, "Scanning networks...");

    // -------------------------------------------------------------
    // Tab 3: Spectrogram Waterfall Heatmap (13 Cols x 8 Time Rows)
    // -------------------------------------------------------------
    lv_obj_t* wf_title = lv_label_create(tab_waterfall);
    lv_label_set_text(wf_title, "Spectrogram Waterfall | History (Top: NOW → Btm: -21s)");
    lv_obj_set_style_text_color(wf_title, lv_color_make(0, 230, 255), LV_OPA_COVER);
    lv_obj_set_style_text_font(wf_title, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_align(wf_title, LV_ALIGN_TOP_LEFT, 2, 0);

    // Grid Container for 8 Rows x 13 Columns
    int cellW = 20;
    int cellH = 17;
    int gridStartX = 32;
    int gridStartY = 16;

    // Time History Y-Labels
    const char* timeLabels[WATERFALL_ROWS] = {"NOW", "-3s", "-6s", "-9s", "-12s", "-15s", "-18s", "-21s"};
    for (int r = 0; r < WATERFALL_ROWS; r++) {
        lv_obj_t* t_lbl = lv_label_create(tab_waterfall);
        lv_label_set_text(t_lbl, timeLabels[r]);
        lv_obj_set_style_text_font(t_lbl, &lv_font_montserrat_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(t_lbl, lv_color_make(150, 160, 180), LV_OPA_COVER);
        lv_obj_align(t_lbl, LV_ALIGN_TOP_LEFT, 0, gridStartY + r * cellH + 2);
    }

    // Build Heatmap Matrix Cells with Explicit Opaque Background Style
    for (int r = 0; r < WATERFALL_ROWS; r++) {
        for (int c = 0; c < WATERFALL_COLS; c++) {
            waterfall_cells[r][c] = lv_obj_create(tab_waterfall);
            lv_obj_set_size(waterfall_cells[r][c], cellW - 2, cellH - 2);
            lv_obj_align(waterfall_cells[r][c], LV_ALIGN_TOP_LEFT, gridStartX + c * cellW, gridStartY + r * cellH);
            lv_obj_set_style_bg_opa(waterfall_cells[r][c], LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(waterfall_cells[r][c], lv_color_make(25, 32, 55), LV_PART_MAIN);
            lv_obj_set_style_border_width(waterfall_cells[r][c], 1, LV_PART_MAIN);
            lv_obj_set_style_border_color(waterfall_cells[r][c], lv_color_make(10, 14, 25), LV_PART_MAIN);
            lv_obj_set_style_radius(waterfall_cells[r][c], 2, LV_PART_MAIN);
            lv_obj_clear_flag(waterfall_cells[r][c], LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    // Bottom X-Axis Channel Number Labels (1 to 13)
    lv_obj_t* wf_x_container = lv_obj_create(tab_waterfall);
    lv_obj_remove_style_all(wf_x_container);
    lv_obj_set_size(wf_x_container, 260, 14);
    lv_obj_align(wf_x_container, LV_ALIGN_TOP_LEFT, gridStartX, gridStartY + WATERFALL_ROWS * cellH + 2);
    lv_obj_set_flex_flow(wf_x_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wf_x_container, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (int ch = 1; ch <= 13; ch++) {
        lv_obj_t* c_lbl = lv_label_create(wf_x_container);
        char cBuf[4];
        snprintf(cBuf, sizeof(cBuf), "%d", ch);
        lv_label_set_text(c_lbl, cBuf);
        lv_obj_set_style_text_font(c_lbl, &lv_font_montserrat_10, LV_PART_MAIN);
        if (ch == 1 || ch == 6 || ch == 11) {
            lv_obj_set_style_text_color(c_lbl, lv_color_make(255, 215, 0), LV_OPA_COVER);
        } else {
            lv_obj_set_style_text_color(c_lbl, lv_color_make(170, 170, 190), LV_OPA_COVER);
        }
    }

    // Explicit Self-Explanatory Color Key Legend on Waterfall Tab
    lv_obj_t* wf_legend = lv_label_create(tab_waterfall);
    lv_label_set_text(wf_legend, "KEY: Blue=Quiet (0)  Green=Clean (1-2)  Yellow=Med  Red=Heavy (6+)");
    lv_obj_set_style_text_color(wf_legend, lv_color_make(180, 200, 220), LV_OPA_COVER);
    lv_obj_set_style_text_font(wf_legend, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_align(wf_legend, LV_ALIGN_TOP_LEFT, 2, 172);
}

void updateUI() {
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        // Update Status Header
        char statusBuf[64];
        snprintf(statusBuf, sizeof(statusBuf), "ESP32 RADAR | Total APs Discovered: %u", wifiCount);
        if (status_label) lv_label_set_text(status_label, statusBuf);

        // 1. Update Spectrum Bar Chart & Channel AP Count Labels
        if (spectrum_chart && series_green && series_yellow && series_red) {
            uint8_t maxCount = 0;
            uint8_t peakChannel = 1;
            uint8_t quietestCh = 1;
            uint8_t minCount = 255;

            for (int ch = 1; ch <= 13; ch++) {
                uint8_t cnt = channelApCount[ch];

                if (cnt <= 2) {
                    lv_chart_set_value_by_id(spectrum_chart, series_green, ch - 1, cnt);
                    lv_chart_set_value_by_id(spectrum_chart, series_yellow, ch - 1, 0);
                    lv_chart_set_value_by_id(spectrum_chart, series_red, ch - 1, 0);
                } else if (cnt <= 5) {
                    lv_chart_set_value_by_id(spectrum_chart, series_green, ch - 1, 0);
                    lv_chart_set_value_by_id(spectrum_chart, series_yellow, ch - 1, cnt);
                    lv_chart_set_value_by_id(spectrum_chart, series_red, ch - 1, 0);
                } else {
                    lv_chart_set_value_by_id(spectrum_chart, series_green, ch - 1, 0);
                    lv_chart_set_value_by_id(spectrum_chart, series_yellow, ch - 1, 0);
                    lv_chart_set_value_by_id(spectrum_chart, series_red, ch - 1, cnt);
                }

                if (cnt > maxCount) {
                    maxCount = cnt;
                    peakChannel = ch;
                }
                if (cnt < minCount) {
                    minCount = cnt;
                    quietestCh = ch;
                }

                if (channel_val_labels[ch]) {
                    if (cnt > 0) {
                        char vBuf[4];
                        snprintf(vBuf, sizeof(vBuf), "%u", cnt);
                        lv_label_set_text(channel_val_labels[ch], vBuf);

                        if (cnt <= 2) {
                            lv_obj_set_style_text_color(channel_val_labels[ch], lv_color_make(0, 255, 120), LV_OPA_COVER);
                        } else if (cnt <= 5) {
                            lv_obj_set_style_text_color(channel_val_labels[ch], lv_color_make(255, 200, 0), LV_OPA_COVER);
                        } else {
                            lv_obj_set_style_text_color(channel_val_labels[ch], lv_color_make(255, 60, 60), LV_OPA_COVER);
                        }
                    } else {
                        lv_label_set_text(channel_val_labels[ch], "");
                    }
                }
            }
            lv_chart_refresh(spectrum_chart);

            if (summary_label) {
                char sumBuf[96];
                if (wifiCount > 0 && channelTopSsid[peakChannel][0] != '\0') {
                    snprintf(sumBuf, sizeof(sumBuf), "Rec: Ch %u (%u APs) | Peak: Ch %u (%s)",
                             quietestCh, (minCount == 255) ? 0 : minCount, peakChannel, channelTopSsid[peakChannel]);
                } else {
                    snprintf(sumBuf, sizeof(sumBuf), "Rec: Ch %u (%u APs) | Peak Ch: %u (%u APs)",
                             quietestCh, (minCount == 255) ? 0 : minCount, peakChannel, maxCount);
                }
                lv_label_set_text(summary_label, sumBuf);
            }
        }

        // 2. Update Rolling Waterfall Spectrogram Matrix
        // Shift history rows down (Row 7 = oldest, Row 0 = newest)
        for (int r = WATERFALL_ROWS - 1; r > 0; r--) {
            for (int c = 0; c < WATERFALL_COLS; c++) {
                waterfallHistory[r][c] = waterfallHistory[r - 1][c];
            }
        }
        for (int c = 0; c < WATERFALL_COLS; c++) {
            waterfallHistory[0][c] = channelApCount[c + 1];
        }

        // Update Cell Background Colors based on AP Congestion
        for (int r = 0; r < WATERFALL_ROWS; r++) {
            for (int c = 0; c < WATERFALL_COLS; c++) {
                if (waterfall_cells[r][c]) {
                    uint8_t cnt = waterfallHistory[r][c];
                    lv_color_t cellColor;
                    if (cnt == 0) {
                        cellColor = lv_color_make(25, 35, 60); // Slate Navy / Quiet
                    } else if (cnt <= 2) {
                        cellColor = lv_color_make(0, 230, 120); // Neon Green / Light
                    } else if (cnt <= 5) {
                        cellColor = lv_color_make(255, 190, 0); // Amber Yellow / Med
                    } else {
                        cellColor = lv_color_make(255, 40, 40); // Crimson Red / Heavy
                    }
                    lv_obj_set_style_bg_opa(waterfall_cells[r][c], LV_OPA_COVER, LV_PART_MAIN);
                    lv_obj_set_style_bg_color(waterfall_cells[r][c], cellColor, LV_PART_MAIN);
                }
            }
        }

        // 3. Update Devices List View without losing user scroll position!
        if (device_list && !inspector_modal) {
            bool is_user_scrolling = lv_obj_is_scrolling(device_list);
            if (!is_user_scrolling) {
                int32_t saved_scroll_y = lv_obj_get_scroll_y(device_list);

                lv_obj_clean(device_list);
                if (wifiCount == 0) {
                    lv_list_add_button(device_list, NULL, "Scanning for Access Points...");
                } else {
                    for (int i = 0; i < wifiCount; i++) {
                        char itemBuf[64];
                        snprintf(itemBuf, sizeof(itemBuf), "%d. %s  (Ch %d | %d dBm)", i + 1, wifiList[i].ssid, wifiList[i].channel, wifiList[i].rssi);
                        lv_obj_t* btn = lv_list_add_button(device_list, NULL, itemBuf);
                        lv_obj_add_event_cb(btn, list_btn_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
                    }
                }

                lv_obj_scroll_to_y(device_list, saved_scroll_y, LV_ANIM_OFF);
            }
        }

        xSemaphoreGive(dataMutex);
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("[System] Starting ESP32 Rolling Waterfall Spectrogram Radar...");

    dataMutex = xSemaphoreCreateMutex();

    // Initialize display hardware, touch controller & backlight
    smartdisplay_init();

    // Set Landscape 90° Rotation (320x240)
    auto disp = lv_display_get_default();
    if (disp) {
        lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);
    }

    smartdisplay_lcd_set_backlight(1.0f);

    buildUI();

    // Launch Scanner Task on Core 0
    xTaskCreatePinnedToCore(
        networkScannerTask,
        "NetScanTask",
        4096,
        NULL,
        1,
        NULL,
        0
    );

    Serial.println("[System] FreeRTOS Scanner launched on Core 0");
}

void loop() {
    static unsigned long last_tick = millis();
    unsigned long now = millis();
    lv_tick_inc(now - last_tick);
    last_tick = now;

    // Refresh UI every 1 second
    static unsigned long last_ui_update = 0;
    if (now - last_ui_update >= 1000) {
        last_ui_update = now;
        updateUI();
    }

    lv_timer_handler();
    delay(5);
}
