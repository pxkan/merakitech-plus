/* src/mp/i18n.c — product name, EN/DA string table, the language file.
 * Part of the merakiplus.c unity build: #included by src/merakiplus.c in a fixed order, never compiled on its own. */
/* ====================================================================== */
/* ================= PRODUCT NAME + I18N (v6.3) ========================= */
/* ====================================================================== */
/* The product name lives in ONE place. UI strings use PRODUCT; the word mark is drawn as BRAND_WORD +
 * "+" badge (brand_at()). "merakiplus" is only the technical identifier: paths, binaries, logs. */
#define BRAND_WORD  "Meraki"
#define BRAND_BADGE "+"
#define PRODUCT     BRAND_WORD BRAND_BADGE

/* control characters in UI strings (interpreted by glyph_next): code spans w. lowercase letters + icons */
#define LC "\x0e"
#define UC "\x0f"
#define IC_BEAN "\x01"
#define IC_CUP "\x02"
#define IC_DROP "\x03"
#define IC_STEAM "\x04"
#define IC_CHECK "\x05"
#define IC_CROSS "\x06"
#define IC_MOON "\x07"
#define IC_WARN "\x08"
#define IC_BOLT "\x09"
#define IC_CLOCK "\x0b"   /* v6.16: clock face (pre-heat); 0x0A is the line break */

/* boot paths (used in the strings below; the logic is in the BOOT-SLOT section) */
#define BOOT_FLAG  "/customer/new"
#define SLOT_A_EXE "/customer/demo_v9"
#define SLOT_B_EXE "/new_customer/demo_v9"
#define SLOT_B_MNT "/new_customer"
#define SW_LOG     "/tmp/switcher.log"            /* written by the switcher daemon (boot/switcher.c) */

/* THE STRING TABLE — the only place a visible text may live. X(id, class, EN, DA).
 *   class = where the text is shown; the selftest measures EVERY string in BOTH languages against the class's
 *   pixel budget (K0 = measured in context by the layout tests, KCON = console).
 *   Rules (enforced by --selftest): both languages filled in; the same printf conversions;
 *   the same numbers/punctuation for numbers and units; only characters the stroke font has; no
 *   generation name; the identifier "merakiplus" only inside code spans (LC … UC) or on the console. */
enum { LANG_EN=0, LANG_DA, NLANG };
enum { K0=0, KT, KSUB, KLAB, KCHIP, KBTN, KBTNS, KTITLE, KPSUB, KSPL, KDL, KDV, KDF, KNOTE, KF1, KF2, KXSUB, KST, KCON, NKLASS };
#define STR_TABLE(X) \
 X(TX_NONE, K0, "", "") \
 X(TX_U_G, K0, "g", "g") \
 X(TX_U_S, K0, "s", "s") \
 X(TX_U_BAR, K0, "bar", "bar") \
 X(TX_U_C, K0, "°C", "°C") \
 X(TX_U_F, K0, "°F", "°F") \
 X(TX_TAGLINE, KSPL, "Advanced Brewing Interface", "Avanceret brygge-interface") \
 X(TX_SPL_CFG_OK, K0, "Config loaded", "Config indlæst") \
 X(TX_SPL_CFG_MISSING, K0, "Config missing", "Config mangler") \
 X(TX_PG_SYSTEM, KTITLE, "System", "System") \
 X(TX_PG_DIAG, KTITLE, "Diagnostics", "Diagnose") \
 X(TX_SEC_DISPLAY, K0, "Display · Sound", "Skærm · lyd") \
 X(TX_SEC_CLOCK, K0, "Clock", "Ur") \
 X(TX_SEC_SCHED, K0, "Schedule", "Tidsplan") \
 X(TX_SEC_STOCK, K0, "Stock UI", "Stock-UI") \
 X(TX_LV_GRIND, K0, "On the scale %s g", "På vægten %s g") \
 X(TX_LV_BREW, K0, "Boiler %s° · group %s°", "Kedel %s° · gruppe %s°") \
 X(TX_LV_WATER, K0, "Water %s°", "Vand %s°") \
 X(TX_LV_STEAM, K0, "Boiler %s° · milk %s°", "Kedel %s° · mælk %s°") \
 X(TX_SL_GRINDW, K0, "Grind Dose", "Kværn-dosis") \
 X(TX_SN_GRINDW, KNOTE, "Stops automatically at this weight", "Stopper automatisk ved denne vægt") \
 X(TX_SL_GRINDMODE, K0, "Grind Mode", "Kværn-tilstand") \
 X(TX_SN_GRINDMODE, KNOTE, PRODUCT " starts weight mode only", PRODUCT " starter kun vægt-tilstand") \
 X(TX_SL_BREWW, K0, "Brew Weight", "Bryg-vægt") \
 X(TX_SN_BREWW, KNOTE, "Stops automatically at this weight", "Stopper automatisk ved denne vægt") \
 X(TX_SL_BREWT, K0, "Brew Temp", "Bryg-temp") \
 X(TX_SN_BREWT, KNOTE, "Target for the brew boiler", "Mål for brygkedlen") \
 X(TX_SL_PISW, K0, "Pre-Infusion", "Pre-infusion") \
 X(TX_SN_PISW, KNOTE, "Soaks the puck before full pressure", "Væder kaffen før fuldt tryk") \
 X(TX_SL_PITIME, K0, "Pre-Inf. Time", "Pre-inf. tid") \
 X(TX_SN_PITIME, KNOTE, "Soak time before full pressure", "Væde-tid før fuldt tryk") \
 X(TX_SL_PIPRESS, K0, "Pre-Inf. Pressure", "Pre-inf. tryk") \
 X(TX_SN_PIPRESS, KNOTE, "The machine only knows 3 and 4", "Maskinen kender kun 3 og 4") \
 X(TX_SL_BREAK, K0, "Pause Time", "Pause-tid") \
 X(TX_SN_BREAK, KNOTE, "No effect · pre-inf. time is used", "Ingen effekt · pre-inf. tid bruges") \
 X(TX_SL_HEADT, K0, "Group Head", "Gruppehoved") \
 X(TX_SN_HEADT, KNOTE, "Target for the group head", "Mål for gruppehovedet") \
 X(TX_SL_STEAMT, K0, "Milk Temp", "Mælketemp") \
 X(TX_SN_STEAMT, KNOTE, "Steam stops at this milk temp", "Dampen stopper ved denne mælketemp") \
 X(TX_SL_STEAMG, K0, "Steam Level", "Damp-niveau") \
 X(TX_SN_STEAMG, KNOTE, "Steam power 1-4", "Damp-styrke 1-4") \
 X(TX_SL_HOTW, K0, "Hot Water", "Varmt vand") \
 X(TX_SN_HOTW, KNOTE, "Stops automatically at this weight", "Stopper automatisk ved denne vægt") \
 X(TX_SL_HOTMODE, K0, "Water Mode", "Vand-tilstand") \
 X(TX_SN_HOTMODE, KNOTE, PRODUCT " starts auto only", PRODUCT " starter kun auto") \
 X(TX_SL_HOTT, K0, "Water Sensor", "Vand-sensor") \
 X(TX_SN_HOTT, KNOTE, "Measured only · no target exists", "Kun målt · intet mål findes") \
 X(TX_SL_UILANG, K0, "UI Language", "UI-sprog") \
 X(TX_SN_UILANG, KNOTE, "Live preview · save to keep", "Live preview · gem for at beholde") \
 X(TX_SL_BL, K0, "Brightness", "Lysstyrke") \
 X(TX_SN_BL, KNOTE, "Live preview", "Live preview") \
 X(TX_SL_VOL, K0, "Volume", "Lydstyrke") \
 X(TX_SN_VOL, KNOTE, "Meraki+ + stock UI sounds", "Meraki+ + stock-UI lyde") \
 X(TX_SH_VOL, K0, "Steps = estimate", "Trin = skøn") \
 X(TX_SL_TUNIT, K0, "Temp Unit", "Temp-enhed") \
 X(TX_SN_TUNIT, KNOTE, "Also applies in " PRODUCT, "Gælder også i " PRODUCT) \
 X(TX_SL_LANG, K0, "Stock Lang.", "Stock-sprog") \
 X(TX_SN_LANG, KNOTE, "Stock UI only", "Kun stock-UI") \
 X(TX_SL_HOTSC, K0, "Water Shortcut", "Vand-genvej") \
 X(TX_SN_HOTSC, KNOTE, "Stock UI home shortcut", "Stock-UI hjem-genvej") \
 X(TX_SL_DST, K0, "Daylight Saving", "Sommertid") \
 X(TX_SL_CLEARTIP, K0, "Cleaning Reminder", "Rense-påmindelse") \
 X(TX_SN_CLEARTIP, KNOTE, "Stock UI tip", "Stock-UI tip") \
 X(TX_SL_TIMEMODE, K0, "Time Format", "Tidsformat") \
 X(TX_SL_AUTOTIME, K0, "Auto Time", "Auto-tid") \
 X(TX_SN_AUTOTIME, KNOTE, "Stock UI network time", "Stock-UI netværkstid") \
 X(TX_SL_TZ, K0, "Time Zone", "Tidszone") \
 X(TX_SN_TZ, KNOTE, "Set it in the stock UI", "Sættes i stock-UI'et") \
 X(TX_SH_EST, K0, "Max = estimate", "Max = skøn") \
 X(TX_E_OFF, K0, "Off", "Fra") \
 X(TX_E_ON, K0, "On", "Til") \
 X(TX_E_WEIGHT, K0, "Weight", "Vægt") \
 X(TX_E_TIME, K0, "Time", "Tid") \
 X(TX_E_AUTO, K0, "Auto", "Auto") \
 X(TX_E_MANUAL, K0, "Manual", "Manuel") \
 X(TX_E_GENTLE, K0, "Gentle", "Blid") \
 X(TX_E_MILD, K0, "Mild", "Mild") \
 X(TX_E_MEDIUM, K0, "Medium", "Medium") \
 X(TX_E_STRONG, K0, "Strong", "Stærk") \
 X(TX_E_24H, K0, "24-Hour", "24-timers") \
 X(TX_E_12H, K0, "12-Hour", "12-timers") \
 X(TX_LG_EN, K0, "English", "Engelsk") \
 X(TX_LG_DE, K0, "German", "Tysk") \
 X(TX_LG_FR, K0, "French", "Fransk") \
 X(TX_LG_IT, K0, "Italian", "Italiensk") \
 X(TX_LG_ES, K0, "Spanish", "Spansk") \
 X(TX_LG_PL, K0, "Polish", "Polsk") \
 X(TX_LG_AR, K0, "Arabic", "Arabisk") \
 X(TX_LG_ZHS, K0, "Chinese Simpl.", "Kinesisk forenkl.") \
 X(TX_LG_ZHT, K0, "Chinese Trad.", "Kinesisk trad.") \
 X(TX_LG_KO, K0, "Korean", "Koreansk") \
 X(TX_LG_JA, K0, "Japanese", "Japansk") \
 X(TX_UL_EN, K0, "English", "English") \
 X(TX_UL_DA, K0, "Dansk", "Dansk") \
 X(TX_PH_OFFLINE, KCHIP, "Offline", "Offline") \
 X(TX_PH_SLEEP, KCHIP, "Asleep", "Sover") \
 X(TX_PH_ERROR, KCHIP, "Error", "Fejl") \
 X(TX_PH_HEAT, KCHIP, "Heating Up", "Varmer op") \
 X(TX_PH_READY, KCHIP, "Ready", "Klar") \
 X(TX_PH_GRIND, KCHIP, "Grinding", "Kværner") \
 X(TX_PH_PRE, KCHIP, "Pre-Infusion", "Pre-infusion") \
 X(TX_PH_EXT, KCHIP, "Extraction", "Ekstraktion") \
 X(TX_PH_WATER, KCHIP, "Hot Water", "Varmt vand") \
 X(TX_PH_STEAM, KCHIP, "Steam", "Damp") \
 X(TX_PH_DONE, KCHIP, "Done", "Færdig") \
 X(TX_PH_STEAM_AUTO, KCHIP, "Steam·Auto", "Damp·auto") \
 X(TX_PH_STEAM_WAND, KCHIP, "Steam·Wand", "Damp·wand") \
 X(TX_ER_E3_BREW, KSUB, "Brew boiler too hot", "Brygkedlen er for varm") \
 X(TX_ER_E3_STEAM, KSUB, "Steam boiler too hot", "Dampkedlen er for varm") \
 X(TX_ER_E1_GROUP, KSUB, "Group head too hot", "Gruppehovedet er for varmt") \
 X(TX_ER_E1_MAINS, KSUB, "Problem with mains voltage", "Problem med netspændingen") \
 X(TX_ER_E2_BREW, KSUB, "Brew boiler sensor fault", "Fejl på brygkedel-sensor") \
 X(TX_ER_E2_GROUP, KSUB, "Group head sensor fault", "Fejl på gruppehoved-sensor") \
 X(TX_ER_E2_STEAM, KSUB, "Steam boiler sensor fault", "Fejl på dampkedel-sensor") \
 X(TX_ER_E2_MILK, KSUB, "Milk sensor fault", "Fejl på mælkesensor") \
 X(TX_ER_UNKNOWN, KSUB, "Unknown fault", "Ukendt fejl") \
 X(TX_A_GRIND, K0, "Grind", "Kværn") \
 X(TX_A_BREW, K0, "Brew", "Bryg") \
 X(TX_A_WATER, K0, "Water", "Vand") \
 X(TX_A_STEAM, K0, "Steam", "Damp") \
 X(TX_G_RO, KT, "Read-only · no writes", "Read-only · ingen writes") \
 X(TX_G_NOLINK, KT, "No connection", "Ingen forbindelse") \
 X(TX_G_ERROR, KT, "Error active · start blocked", "Fejl aktiv · start spærret") \
 X(TX_G_SLEEP, KT, "Waking up · one moment", "Vågner · et øjeblik") \
 X(TX_G_BUSY, KT, "Activity in progress", "Aktivitet i gang") \
 X(TX_G_WAIT, KT, "One moment…", "Et øjeblik…") \
 X(TX_G_NOCFG, KT, "Config missing · start blocked", "Config mangler · start spærret") \
 X(TX_G_OOR, KT, "Config out of spec · fix first", "Config uden for spec · ret først") \
 X(TX_G_GRINDTIME, KT, "Grind = time · use stock UI", "Kværn = tid · brug stock-UI") \
 X(TX_G_WATERMAN, KT, "Water = manual · use stock UI", "Vand = manuel · brug stock-UI") \
 X(TX_T_POLICY, KT, "Blocked by a safety rule", "Spærret af en sikkerhedsregel") \
 X(TX_T_QFULL, KT, "Busy · try again", "Optaget · prøv igen") \
 X(TX_T_RANGE, KT, "Out of range", "Uden for grænser") \
 X(TX_T_CFG_UNAVAIL, KT, "Config unavailable · not saved", "Config utilgængelig · intet gemt") \
 X(TX_T_NOLINK_SAVE, KT, "No connection · not saved", "Ingen forbindelse · intet gemt") \
 X(TX_T_WAIT_ACT, KT, "Wait until the activity ends", "Vent til aktivitet er slut") \
 X(TX_T_WRITE_REJ, KT, "Not accepted · not saved", "Ikke accepteret · intet gemt") \
 X(TX_T_NOACK_SAVE, KT, "Save failed · no reply", "Gem fejlet · intet svar") \
 X(TX_T_SAVED_MCU, KT, "Saved · active now", "Gemt · aktiv nu") \
 X(TX_T_SAVED_CFG, KT, "Saved", "Gemt") \
 X(TX_T_CFG_ERR, KT, "Config error · not saved", "Config-fejl · intet gemt") \
 X(TX_T_LANG_SAVED, KT, "Language saved · English", "Sprog gemt · dansk") \
 X(TX_T_LANG_NOFILE, KT, "Language active · file not saved", "Sprog aktivt · fil ikke gemt") \
 X(TX_T_DRYRUN, KT, "Dry run · see " LC "cfgbak/boot_ui.log", "Dry-run · se " LC "cfgbak/boot_ui.log") \
 X(TX_T_SW_FAIL_NOBIN, KT, "Switch failed · no stock binary", "Skift fejlede · ingen stock-binær") \
 X(TX_T_SW_FAIL_FORK, KT, "Switch failed · fork error", "Skift fejlede · fork-fejl") \
 X(TX_T_SW_FAIL_DIED, KT, "Switch failed · stock UI exited", "Skift fejlede · stock-UI stoppede") \
 X(TX_T_BOOT_SAVED_OURS, KT, "Saved · next restart: " PRODUCT, "Gemt · næste genstart: " PRODUCT) \
 X(TX_T_BOOT_SAVED_ORIG, KT, "Saved · next restart: original", "Gemt · næste genstart: original") \
 X(TX_T_BOOT_LOCKED, KT, "Locked · flag not changed", "Låst · flag ikke ændret") \
 X(TX_T_BOOT_ERR, KT, "Flag error · nothing changed", "Flag-fejl · intet ændret") \
 X(TX_T_STOP_SENT, KT, "Stopping steam…", "Stopper damp…") \
 X(TX_T_STOP_GRIND, KT, "Stopping grind…", "Stopper kværn…") \
 X(TX_T_STOP_BREW, KT, "Stopping brew…", "Stopper bryg…") \
 X(TX_T_STOP_WATER, KT, "Stopping water…", "Stopper vand…") \
 X(TX_T_STOPPED_GRIND, KT, "Grind stopped", "Kværn stoppet") \
 X(TX_T_STOPPED_BREW, KT, "Brew stopped", "Bryg stoppet") \
 X(TX_T_STOPPED_WATER, KT, "Water stopped", "Vand stoppet") \
 X(TX_T_STOPPED_STEAM, KT, "Steam stopped", "Damp stoppet") \
 X(TX_T_STOP_NOACK, KT, "Stop: no reply · try again", "Stop: intet svar · prøv igen") \
 X(TX_T_STOP_UNCONF, KT, "Stop not confirmed · try again", "Stop ikke bekræftet · prøv igen") \
 X(TX_T_STOP_FAIL, KT, "Stop not sent · try again", "Stop ikke sendt · prøv igen") \
 X(TX_T_PW_BUSY, KT, "Stop the activity first · then off", "Stop aktiviteten først · så sluk") \
 X(TX_T_PW_NOLINK, KT, "No connection · cannot turn off", "Ingen forbindelse · slukker ikke") \
 X(TX_T_PW_NOACK, KT, "Power off: no reply · try again", "Sluk: intet svar · prøv igen") \
 X(TX_T_PW_ON, KT, "Machine is on", "Maskinen er tændt") \
 X(TX_T_HOLD_HINT, KT, "Keep holding to start", "Bliv ved at holde for start") \
 X(TX_T_PINFO, K0, "Pre-inf. %d bar = raw %d · linear", "Præ-inf. %d bar = rå %d · lineær") \
 X(TX_T_NOACK, KT, "The machine did not answer", "Maskinen svarede ikke") \
 X(TX_T_NOSTART, KT, "No start · check the machine", "Ingen start · tjek maskinen") \
 X(TX_H_DOSE, KLAB, "Dose", "Dosis") \
 X(TX_H_CUP, KLAB, "In the Cup", "I koppen") \
 X(TX_H_MILK_TGT, KLAB, "Milk · Target %s°", "Mælk · mål %s°") \
 X(TX_H_MILK, KLAB, "Milk", "Mælk") \
 X(TX_H_MILK_PROBE, KLAB, "Milk Probe", "Mælkeprobe") \
 X(TX_H_BOILER, KLAB, "Brew Boiler", "Brygkedel") \
 X(TX_H_CRC, K0, "CRC %u", "CRC %u") \
 X(TX_H_DONE_IN, KSUB, "Done in %s", "Færdig på %s") \
 X(TX_H_AUTOSTOP_G, KSUB, "Stops at %s g", "Stopper ved %s g") \
 X(TX_H_AUTOSTOP_T, KSUB, "Stops at %s°", "Stopper ved %s°") \
 X(TX_H_WAND_NOSTOP, KSUB, "Release the wand to stop", "Slip wand for at stoppe") \
 X(TX_H_DONE_RISE, KSUB, "Done in %s · rises ≈%s° more", "Færdig på %s · stiger ≈%s° til") \
 X(TX_H_NORESP, KSUB, "No connection to the machine", "Ingen forbindelse til maskinen") \
 X(TX_H_WAKING, KSUB, "Waking up…", "Vågner…") \
 X(TX_H_HEATING_TO, KSUB, "Heating to %s°", "På vej mod %s°") \
 X(TX_H_SET_TO, KSUB, "Target %s°", "Mål %s°") \
 X(TX_H_STARTING, KSUB, "Starting %s…", "Starter %s…") \
 X(TX_H_HOLD_GRIND, KSUB, "Hold · grind %s g", "Hold · kværn %s g") \
 X(TX_H_HOLD_BREW, KSUB, "Hold · brew %s g", "Hold · bryg %s g") \
 X(TX_H_HOLD_WATER, KSUB, "Hold · hot water %s g", "Hold · varmt vand %s g") \
 X(TX_H_HOLD_STEAM, KSUB, "Hold · steam to %s°", "Hold · damp til %s°") \
 X(TX_ST_TIME, K0, "Time", "Tid") \
 X(TX_ST_BAR, K0, "≈bar", "≈bar") \
 X(TX_ST_PRAW, K0, "Raw", "Rå") \
 X(TX_ST_PULSE, K0, "Pulse", "Puls") \
 X(TX_ST_FLOW, K0, "Flow", "Flow") \
 X(TX_ST_CURRENT, K0, "Current", "Strøm") \
 X(TX_ST_GRINDSTEP, K0, "Step", "Trin") \
 X(TX_ST_BOILER, K0, "Boiler", "Kedel") \
 X(TX_ST_STEAMBOILER, K0, "Steam", "Damp") \
 X(TX_ST_LEVEL, K0, "Level", "Niveau") \
 X(TX_ST_GROUP, K0, "Group", "Gruppe") \
 X(TX_ST_WEIGHT, K0, "Weight", "Vægt") \
 X(TX_ST_MILK, K0, "Milk", "Mælk") \
 X(TX_C_HINT, K0, "Hold = start · tap = settings", "Hold = start · tryk = indstil") \
 X(TX_C_PREHEAT, K0, IC_CLOCK " Pre-heat · %s %s", IC_CLOCK " Forvarmning · %s %s") \
 X(TX_C_ERRCODE, K0, "Code %s · tap for details", "Kode %s · tryk for detaljer") \
 X(TX_C_DIAGREF, K0, "Tap for details", "Tryk for detaljer") \
 X(TX_C_PRESS, K0, "Pressure", "Tryk") \
 X(TX_C_WEIGHT, K0, "Weight", "Vægt") \
 X(TX_C_MILK, K0, "Milk Temp", "Mælketemp") \
 X(TX_C_TARGET, K0, "- - Target", "- - Mål") \
 X(TX_C_RAW, K0, "Raw", "Rå") \
 X(TX_B_STOP_STEAM, K0, "Stop Steam", "Stop damp") \
 X(TX_B_STOP_GRIND, K0, "Stop Grind", "Stop kværn") \
 X(TX_B_STOP_BREW, K0, "Stop Brew", "Stop bryg") \
 X(TX_B_STOP_WATER, K0, "Stop Water", "Stop vand") \
 X(TX_B_START, K0, "Hold to Start", "Hold for start") \
 X(TX_B_BACK, KBTN, "Back", "Tilbage") \
 X(TX_B_CANCEL, KBTN, "Cancel", "Annuller") \
 X(TX_B_SAVE, KBTNS, "Save", "Gem") \
 X(TX_B_SAVING, KBTNS, "Saving", "Gemmer") \
 X(TX_B_SWITCH, KBTN, "Switch Now", "Skift nu") \
 X(TX_B_EXIT, KBTNS, "Exit", "Afslut") \
 X(TX_X_BOOTUI, K0, "Boot UI", "Boot-UI") \
 X(TX_X_SWITCH, K0, "Switch to Original Now", "Skift til original nu") \
 X(TX_X_SWITCH_SUB, KXSUB, "Immediate · no restart", "Straks · uden genstart") \
 X(TX_BN_ORIGINAL, K0, "Original", "Original") \
 X(TX_BN_OURS, K0, PRODUCT, PRODUCT) \
 X(TX_BN_SLOTB, K0, "Slot B", "Slot B") \
 X(TX_BL_NOMOUNT, KXSUB, "Locked · slot B not mounted", "Låst · slot B ikke mountet") \
 X(TX_BL_NOMOUNT_L, KNOTE, LC SLOT_B_MNT UC " is not mounted", LC SLOT_B_MNT UC " er ikke mountet") \
 X(TX_BL_NOBIN, KXSUB, "Locked · slot B has no " LC "demo_v9", "Låst · slot B uden " LC "demo_v9") \
 X(TX_BL_NOBIN_L, KNOTE, LC SLOT_B_EXE UC " is missing", LC SLOT_B_EXE UC " mangler") \
 X(TX_BL_VENDOR, KXSUB, "Locked · slot B is not " PRODUCT, "Låst · slot B er ikke " PRODUCT) \
 X(TX_BL_VENDOR_L, KNOTE, LC SLOT_B_EXE UC " is not " PRODUCT, LC SLOT_B_EXE UC " er ikke " PRODUCT) \
 X(TX_BS_OURS, K0, "Now: " PRODUCT " (B) at boot", "Nu: " PRODUCT " (B) ved boot") \
 X(TX_BS_ORIG, K0, "Now: original (A) at boot", "Nu: original (A) ved boot") \
 X(TX_BS_SLOTB, K0, "Now: slot B (not " PRODUCT ") at boot", "Nu: slot B (ikke " PRODUCT ") ved boot") \
 X(TX_BH1, KXSUB, "Applies at next restart", "Gælder næste genstart") \
 X(TX_BH2, KF1, "OTA may overwrite this", "OTA kan overskrive dette") \
 X(TX_BH3, KF2, "Choice — see BOOT" LC ".md" UC " §7", "Valg — se BOOT" LC ".md" UC " §7") \
 X(TX_SW_LOCK, KXSUB, "Locked · stock binary missing", "Låst · stock-binær mangler") \
 X(TX_SW_LOCK_L, KNOTE, LC SLOT_A_EXE UC " is missing", LC SLOT_A_EXE UC " mangler") \
 X(TX_D_MEASURED, K0, "Measured value · not a setting", "Målt værdi · ingen indstilling") \
 X(TX_D_NOEDIT, K0, "Cannot be changed here", "Kan ikke ændres her") \
 X(TX_D_NOPTS, K0, "%d options", "%d valg") \
 X(TX_D_SAVING, KST, "Saving…", "Gemmer…") \
 X(TX_D_OOR, KST, "Out of spec · pick a new value", "Uden for spec · vælg ny værdi") \
 X(TX_D_HOLDFAST, KST, "Hold - or + to go fast", "Hold - eller + for hurtigt") \
 X(TX_D_CHANGED, K0, "Changed · was %s %s", "Ændret · før %s %s") \
 X(TX_D_LOCKED, KST, "Locked", "Låst") \
 X(TX_Y_CHANGED, K0, "Changed · was %s", "Ændret · før %s") \
 X(TX_Y_FLAG_YES, KF1, "Flag " LC BOOT_FLAG UC " exists", "Flag " LC BOOT_FLAG UC " findes") \
 X(TX_Y_FLAG_NO, KF1, "No flag " LC BOOT_FLAG, "Intet flag " LC BOOT_FLAG) \
 X(TX_Y_FLAG_HANDSOFF, KF2, "Flag is not touched here", "Flaget røres ikke herfra") \
 X(TX_Y_NEXT, K0, "Next restart: %s", "Næste genstart: %s") \
 X(TX_Y_NOSTOCK, KF1, "No stock UI to start", "Intet stock-UI at starte") \
 X(TX_Y_STAYS, KF2, PRODUCT " keeps running", PRODUCT " bliver kørende") \
 X(TX_Y_CLOSE_CONT, KST, PRODUCT " closes · stock UI resumes", PRODUCT " lukker · stock-UI fortsætter") \
 X(TX_Y_CLOSE_START, KST, PRODUCT " closes · stock UI starts", PRODUCT " lukker · stock-UI starter") \
 X(TX_Y_REMINDER_FOLLOWS, KNOTE, "Config changed · reminder follows", "Config ændret · påmindelse følger") \
 X(TX_Y_PAUSED_PID, KNOTE, "Stock UI is paused · PID %d", "Stock-UI er pauset · PID %d") \
 X(TX_Y_RUNSH, KF1, "run.sh mode · clean exit", "run.sh-tilstand · ren exit") \
 X(TX_Y_BACK_RUNSH, KF2, "Back here: run.sh", "Tilbage hertil: run.sh") \
 X(TX_Y_FRESH, KNOTE, "Fresh start · reads current config", "Frisk start · læser aktuel config") \
 X(TX_Y_BACK_GESTURE1, KF1, "Back here: 3 long presses", "Tilbage hertil: 3 lange tryk") \
 X(TX_Y_BACK_GESTURE2, KF2, "Top left corner (1.5 s)", "Øverst til venstre (1.5 s)") \
 X(TX_Y_NOSWITCHER, KF1, "Switcher daemon not running", "Switcher-daemon kører ikke") \
 X(TX_Y_BACK_RESTART, KF2, "Back here: restart", "Tilbage hertil: genstart") \
 X(TX_Y_BACK_TELNET, KF2, "Back here: telnet only", "Tilbage hertil: kun telnet") \
 X(TX_Y_EXIT_TITLE, K0, "Config has changed", "Config er ændret") \
 X(TX_Y_EXIT_SUB, K0, "Stock UI has old values", "Stock-UI har gamle værdier") \
 X(TX_Y_EXIT_NOTE, KNOTE, "Run via telnet · watchdog restarts it", "Kør via telnet · watchdog starter den igen") \
 X(TX_Y_EXIT_F1, KF1, "Otherwise stock UI sends", "Ellers sender stock-UI") \
 X(TX_Y_EXIT_F2, KF2, "Old values when brewing", "Gamle værdier ved bryg") \
 X(TX_Y_EXIT_L1, K0, "Stock UI needs a restart of", "Stock-UI kræver genstart af") \
 X(TX_Y_EXIT_L2, K0, LC "demo_v9" UC " for new values", LC "demo_v9" UC " for nye værdier") \
 X(TX_Y_EXIT_CMD, K0, LC "kill $(pidof demo_v9)", LC "kill $(pidof demo_v9)") \
 X(TX_Y_BYE_T, K0, "Switching to original", "Skifter til original") \
 X(TX_Y_BYE_CONT, K0, "Stock UI resumes", "Stock-UI fortsætter") \
 X(TX_Y_BYE_START, K0, "Stock UI starting · one moment", "Stock-UI starter · et øjeblik") \
 X(TX_Y_BYE_CLOSED, K0, PRODUCT " has closed", PRODUCT " er afsluttet") \
 X(TX_PW_OFFING, K0, "Turning off…", "Slukker…") \
 X(TX_PW_OFF, K0, "Off", "Slukket") \
 X(TX_PW_WAKING, K0, "Turning on…", "Tænder…") \
 X(TX_PW_SUB_OFFING, K0, "The machine goes to sleep", "Maskinen går i dvale") \
 X(TX_PW_SUB_OFF, K0, "Hold the power button to turn on", "Hold knappen inde for at tænde") \
 X(TX_PW_SUB_WAKING, K0, "Keep holding the button a moment", "Hold knappen inde et øjeblik") \
 X(TX_PW_NOTE_OFF, K0, "The screen turns off · touch to show", "Skærmen slukker · rør for at vise") \
 X(TX_DL_LINK, K0, "Link", "Link") \
 X(TX_DL_PHASE, K0, "Phase", "Fase") \
 X(TX_DL_RXOK, K0, "RX OK", "RX OK") \
 X(TX_DL_RXCRC, K0, "RX CRC / Resync", "RX CRC / resync") \
 X(TX_DL_WORKSTA, K0, "WORK_STA", "WORK_STA") \
 X(TX_DL_E123, K0, "E1 / E2 / E3", "E1 / E2 / E3") \
 X(TX_DL_CMD6, K0, "CMD6 OK·RTX·ERR·BLK", "CMD6 OK·RTX·fejl·blok") \
 X(TX_DL_AWAKE, K0, "Awake (word 1)", "Vågen (ord 1)") \
 X(TX_DL_MAINS, K0, "Mains", "Net") \
 X(TX_DL_CURRENT, K0, "Current 1 / 2", "Strøm 1 / 2") \
 X(TX_DL_FRAMEMS, K0, "Frame ms Now/Max", "Frame ms nu/max") \
 X(TX_DL_MIRROR, K0, "Mirror 112-114", "Spejl 112-114") \
 X(TX_DL_CFGMD5, K0, "Config MD5", "Config MD5") \
 X(TX_DL_VERSION, K0, "Version", "Version") \
 X(TX_DL_BP_A, K0, "Boot Push 1-30", "Boot-push 1-30") \
 X(TX_DL_BP_B, K0, "Boot Push 112-114", "Boot-push 112-114") \
 X(TX_DL_BP_A_RE, K0, "Re-Push %d · 1-30", "Re-push %d · 1-30") \
 X(TX_DL_BP_B_RE, K0, "112-114", "112-114") \
 X(TX_DL_FLAG, KDL, "Flag " LC BOOT_FLAG, "Flag " LC BOOT_FLAG) \
 X(TX_DL_NEXTBOOT, KDL, "Next Boot", "Næste boot") \
 X(TX_DL_STARTED, KDL, "Started from", "Startet fra") \
 X(TX_DL_SLOTB, KDL, "Slot B", "Slot B") \
 X(TX_DL_SWITCHER, KDL, "Switcher Daemon", "Switcher-daemon") \
 X(TX_DL_STOCK, KDL, "Stock Process", "Stock-proces") \
 X(TX_DV_OK, KDV, "OK", "OK") \
 X(TX_DV_DOWN, KDV, "Down", "Nede") \
 X(TX_DV_YES, KDV, "Yes", "Ja") \
 X(TX_DV_NO, KDV, "No", "Nej") \
 X(TX_DV_MAINS, K0, "%u V · %u Hz", "%u V · %u Hz") \
 X(TX_DV_VERSION, K0, PRODUCT " " MERAKIPLUS_VERSION, PRODUCT " " MERAKIPLUS_VERSION) \
 X(TX_DV_VERSION_RO, K0, PRODUCT " " MERAKIPLUS_VERSION " RO", PRODUCT " " MERAKIPLUS_VERSION " RO") \
 X(TX_DV_MISSING, KDV, "Missing", "Mangler") \
 X(TX_DV_FLAG_YES, KDV, "Exists", "Findes") \
 X(TX_DV_FLAG_NO, KDV, "Not present", "Findes ikke") \
 X(TX_DV_SLOTA, KDV, "Slot A", "Slot A") \
 X(TX_DV_SLOTB, KDV, "Slot B", "Slot B") \
 X(TX_DV_RUNSH, KDV, "run.sh", "run.sh") \
 X(TX_DV_UNKNOWN, KDV, "Unknown", "Ukendt") \
 X(TX_DV_B_OK, KDV, PRODUCT " found", PRODUCT " fundet") \
 X(TX_DV_B_NOMOUNT, KDV, "Not mounted", "Ikke mountet") \
 X(TX_DV_B_NOBIN, KDV, "No demo_v9", "Uden demo_v9") \
 X(TX_DV_B_VENDOR, KDV, "Not " PRODUCT, "Ikke " PRODUCT) \
 X(TX_DV_RUNNING, KDV, "Running", "Kører") \
 X(TX_DV_NOTRUNNING, KDV, "Not running", "Kører ikke") \
 X(TX_DV_NONE, KDV, "None", "Ingen") \
 X(TX_DV_ALSO, KDV, "Also running!", "Kører samtidig!") \
 X(TX_DV_PAUSED, KDV, "Paused", "Pauset") \
 X(TX_DV_BP_OFF, KDV, "Off", "Fra") \
 X(TX_DV_BP_WAIT, KDV, "Waiting", "Venter") \
 X(TX_DV_BP_SENT, KDV, "Sending", "Sender") \
 X(TX_DV_BP_NOACK, KDV, "No ACK", "Ingen ACK") \
 X(TX_DV_BP_BLOCKED, KDV, "Rejected", "Afvist") \
 X(TX_DV_BP_NOCFG, KDV, "No config", "Ingen config") \
 X(TX_DV_BP_SKIP, KDV, "Skipped", "Sprunget over") \
 X(TX_DG_CAP, KDF, "Boot · Slot · Switcher", "Boot · slot · switcher") \
 X(TX_DG_L_PID, KDF, "PID · " PRODUCT " / Stock UI / Switcher", "PID · " PRODUCT " / stock-UI / switcher") \
 X(TX_DG_L_EXE, KDF, "Running from · " LC "/proc/self/exe", "Kører fra · " LC "/proc/self/exe") \
 X(TX_DG_L_EVT, KDF, "Last Switch · " LC SW_LOG, "Seneste skift · " LC SW_LOG) \
 X(TX_DG_L_NET, KDF, "Network · WPA PID / IP / State", "Netværk · WPA-PID / IP / tilstand") \
 X(TX_NS_OFF, K0, "Off", "Fra") \
 X(TX_NS_DOWN, K0, "Down", "Nede") \
 X(TX_NS_STARTING, K0, "Starting", "Starter") \
 X(TX_NS_CONNECTING, K0, "Connecting", "Forbinder") \
 X(TX_NS_UP, K0, "Up", "Oppe") \
 X(TX_DG_NOEXE, KDF, "Not readable", "Ikke læsbar") \
 X(TX_DG_NOEVT, KDF, "No switch event in the log", "Ingen skift-hændelse i loggen") \
 X(TX_DG_NOLOG, KDF, "Log not readable", "Log ikke læsbar") \
 X(TX_DG_L_GRAPH, KDF, "Graph · Tap to Toggle", "Graf · tryk for at skifte") \
 X(TX_DV_G_SMOOTH, KDF, "Smooth · median 3 + EMA", "Glat · median 3 + EMA") \
 X(TX_DV_G_RAW, KDF, "Raw · unfiltered", "Rå · ufiltreret") \
 X(TX_DG_PCAP, KDF, "Pressure · Estimate", "Tryk · estimat") \
 X(TX_DG_L_PANCH, KDF, "Anchor · Pre-Inf. in Recording", "Anker · præ-inf. i optagelsen") \
 X(TX_DV_PANCH, K0, "%d bar = raw %d · " LC "k" UC " %.5f", "%d bar = rå %d · " LC "k" UC " %.5f") \
 X(TX_DG_L_PNOW, KDF, "Now · Word 51 · Pump Side, Linear", "Nu · ord 51 · pumpe-side, lineær") \
 X(TX_DV_PNOW, K0, "Raw %d = ≈%.1f bar", "Rå %d = ≈%.1f bar") \
 X(TX_DV_PPULSE, K0, "Raw %d · pulse, no pressure", "Rå %d · puls, ikke tryk") \
 X(TX_DV_PMAINT, K0, "Raw %d · maintenance pulse", "Rå %d · vedligehold-puls") \
 X(TX_DG_ABOUT, KDF, "About", "Om") \
 X(TX_DG_RAWCAP, KDF, "Raw Counters · Service", "Rå tællere · service") \
 X(TX_DG_L_LANG, KDF, "UI Language · " LC "merakiplus/lang", "UI-sprog · " LC "merakiplus/lang") \
 X(TX_DG_LANG_DEFAULT, K0, "%s · default (no file)", "%s · standard (ingen fil)") \
 X(TX_DG_LANG_FILE, K0, "%s · from file", "%s · fra fil") \
 X(TX_DG_LANG_UNKNOWN, K0, "%s · unknown file value", "%s · ukendt værdi i fil") \
 X(TX_DG_LANG_SESSION, K0, "%s · not saved", "%s · ikke gemt") \
 X(TX_DG_PWCAP, KDF, "Power Button", "Tænd/sluk-knap") \
 X(TX_DG_L_PWNODE, KDF, "Button Node · evdev · No Grab", "Knap-node · evdev · intet grab") \
 X(TX_DL_PW_STATE, KDL, "State", "Tilstand") \
 X(TX_DL_PW_W0, KDL, "Power · Word 0", "Strøm · ord 0") \
 X(TX_DL_PW_COUNT, KDL, "Press · Off · On", "Tryk · sluk · tænd") \
 X(TX_DL_PW_LAST, KDL, "Last Key", "Sidste knap") \
 X(TX_DV_PW_ON, KDV, "On", "Tændt") \
 X(TX_DV_PW_OFFING, KDV, "Turning off", "Slukker") \
 X(TX_DV_PW_OFF, KDV, "Off", "Slukket") \
 X(TX_DV_PW_WAKING, KDV, "Turning on", "Tænder") \
 X(TX_DV_PW_NOREPLY, KDV, "No reply", "Intet svar") \
 X(TX_DV_PW_KEY, K0, "%d = %d · %ld s", "%d = %d · %ld s") \
 X(TX_DV_PW_NOKEY, KDV, "No press yet", "Ingen tryk") \
 X(TX_DV_PW_NONODE, KDF, "No button node found", "Ingen knap-node fundet") \
 X(TX_SL_PREHEAT, K0, "Pre-Heat", "Forvarmning") \
 X(TX_SL_PHAT, K0, "Pre-Heat At", "Forvarm kl.") \
 X(TX_SL_PHREP, K0, "Repeat", "Gentag") \
 X(TX_SL_AUTOOFF, K0, "Auto Off", "Auto-sluk") \
 X(TX_SN_PREHEAT, KNOTE, "The machine wakes + heats itself", "Maskinen vågner + varmer selv") \
 X(TX_SN_PHAT, KNOTE, "Local time", "Lokal tid") \
 X(TX_SN_PHREP, KNOTE, "Once = switches itself off after use", "En gang = slår sig selv fra bagefter") \
 X(TX_SN_AUTOOFF, KNOTE, "No touch or activity · then off", "Ingen berøring eller aktivitet · så sluk") \
 X(TX_SN_TIMEFMT, KNOTE, "Home clock + stock UI clock", "Ur på hjem + stock-UI'ets ur") \
 X(TX_E_ONCE, K0, "Once", "En gang") \
 X(TX_E_DAILY, K0, "Every Day", "Hver dag") \
 X(TX_E_WORKDAYS, K0, "Mon-Fri", "Man-fre") \
 X(TX_E_WEEKEND, K0, "Sat-Sun", "Lør-søn") \
 X(TX_E_MIN15, K0, "15 min", "15 min") \
 X(TX_E_MIN20, K0, "20 min", "20 min") \
 X(TX_E_MIN30, K0, "30 min", "30 min") \
 X(TX_E_MIN60, K0, "60 min", "60 min") \
 X(TX_T_OWN_SAVED, KT, "Schedule saved", "Tidsplan gemt") \
 X(TX_T_OWN_NOFILE, KT, "Active · file not saved", "Aktiv · fil ikke gemt") \
 X(TX_SL_SOUND, K0, "Sound", "Lyd") \
 X(TX_SL_SNDRDY, K0, "Ready Tone", "Klar-tone") \
 X(TX_SN_SOUND, KNOTE, "Start jingle + boiler ready tones", "Start-jingle + klar-toner") \
 X(TX_SN_SNDRDY, KNOTE, "Its own tone per boiler · when hot", "Sin egen tone pr. kedel · når varm") \
 X(TX_E_SND_STOCK, K0, "Stock Sounds", "Stock-lyde") \
 X(TX_E_RDY_BREW, K0, "Brew Boiler", "Brygkedel") \
 X(TX_E_RDY_STEAM, K0, "Steam Boiler", "Dampkedel") \
 X(TX_E_RDY_BOTH, K0, "Both Boilers", "Begge kedler") \
 X(TX_T_SND_SAVED, KT, "Sound saved", "Lyd gemt") \
 X(TX_DG_SNDCAP, KDF, "Sound", "Lyd") \
 X(TX_DL_SND, KDL, "Sound · Volume", "Lyd · lydstyrke") \
 X(TX_DL_SND_LAST, KDL, "Last Sound", "Sidste lyd") \
 X(TX_DV_SND_ON, KDV, "On · %d", "Til · %d") \
 X(TX_DV_SND_STOCK, KDV, "Stock · %d", "Stock · %d") \
 X(TX_DV_SND_OFF, KDV, "Off", "Fra") \
 X(TX_DV_SND_MUTE, KDV, "Volume 0", "Lydstyrke 0") \
 X(TX_DV_SND_NOHELPER, KDV, "No player", "Ingen afspiller") \
 X(TX_DV_SE_JINGLE, KDV, "Jingle", "Jingle") \
 X(TX_DV_SE_BREW, KDV, "Brew", "Bryg") \
 X(TX_DV_SE_STEAM, KDV, "Steam", "Damp") \
 X(TX_DV_SE_PREVIEW, KDV, "Test", "Test") \
 X(TX_DV_SO_PLAYED, KDV, "Played", "Spillet") \
 X(TX_DV_SO_MUTED, KDV, "Muted", "Lydløs") \
 X(TX_DV_SO_SKIP, KDV, "Skipped", "Udeladt") \
 X(TX_DV_SO_FAIL, KDV, "Failed", "Fejl") \
 X(TX_DV_SO_DROP, KDV, "Busy", "Optaget") \
 X(TX_T_AUTOOFF_SOON, KT, "Auto off in 30 s · touch to stay on", "Auto-sluk om 30 s · rør skærmen") \
 X(TX_T_ONCE_USED, KT, "One-time pre-heat used · now off", "Forvarmning brugt · nu fra") \
 X(TX_T_NOCLOCK, KT, "Clock not set · no pre-heat alarm", "Ur ikke sat · ingen forvarm-alarm") \
 X(TX_AM, K0, "A.M.", "A.M.") \
 X(TX_PM, K0, "P.M.", "P.M.") \
 X(TX_WD_SUN, K0, "Sun", "Søn") \
 X(TX_WD_MON, K0, "Mon", "Man") \
 X(TX_WD_TUE, K0, "Tue", "Tir") \
 X(TX_WD_WED, K0, "Wed", "Ons") \
 X(TX_WD_THU, K0, "Thu", "Tor") \
 X(TX_WD_FRI, K0, "Fri", "Fre") \
 X(TX_WD_SAT, K0, "Sat", "Lør") \
 X(TX_DG_TCAP, KDF, "Time · Schedule", "Tid · plan") \
 X(TX_DL_T_CLOCK, KDL, "Local Time", "Lokal tid") \
 X(TX_DL_T_ZONE, KDL, "Zone · DST", "Zone · sommertid") \
 X(TX_DL_T_AUTOOFF, KDL, "Auto Off · Idle", "Auto-sluk · hvile") \
 X(TX_DL_T_ALARM, KDL, "Alarm Sent · d h:m", "Alarm sendt · d t:m") \
 X(TX_DG_L_NEXT, KDF, "Next Pre-Heat · Controller Alarm", "Næste forvarmning · alarm") \
 X(TX_DV_T_NOTSET, KDV, "Not set", "Ikke sat") \
 X(TX_DV_T_NONE, KDV, "None", "Ingen") \
 X(TX_DV_T_NOSCHED, KDF, "No pre-heat planned", "Ingen forvarmning planlagt") \
 X(TX_DV_T_DST, K0, "%s · DST", "%s · sommertid") \
 X(TX_DV_T_STD, K0, "%s · standard", "%s · normaltid") \
 X(TX_DV_T_IDLE, K0, "%s · %d:%02d", "%s · %d:%02d") \
 X(TX_O_SWITCHED, KCON, "== merakiplus: runtime switch B->A (user button): stock UI pid %d started from " SLOT_A_EXE "; merakiplus exits\n", "== merakiplus: runtime-skift B->A (bruger-knap): stock-UI pid %d startet fra " SLOT_A_EXE "; merakiplus afslutter\n") \
 X(TX_O_RESTART, KCON, "== merakiplus: STOCK UI NEEDS A RESTART OF demo_v9 FOR NEW VALUES:   kill $(pidof demo_v9)\n==        (config.json changed by merakiplus; the watchdog restarts it, the boot push syncs the controller)\n", "== merakiplus: STOCK-UI KRAEVER GENSTART AF demo_v9 FOR NYE VAERDIER:   kill $(pidof demo_v9)\n==        (config.json aendret af merakiplus; watchdog genstarter, boot-push synker controlleren)\n") \
 X(TX_O_CFGCHANGED, KCON, "== merakiplus: config.json changed %d time(s) - no paused stock UI is waiting; the next stock start reads it itself\n", "== merakiplus: config.json aendret %d gang(e) - intet pauset stock-UI venter; naeste stock-start laeser den selv\n") \
 X(TX_O_BLOCKED, KCON, "== merakiplus: %ld cmd6 frame(s) rejected by the frame policy (see UI_v6.md)\n", "== merakiplus: %ld cmd6-frame(s) afvist af frame-politikken (se UI_v6.md)\n") \
 /* end */

enum {
#define X(id,k,en,da) id,
 STR_TABLE(X)
#undef X
 NSTR };
static const char*const STR[NLANG][NSTR]={
 [LANG_EN]={
#define X(id,k,en,da) en,
 STR_TABLE(X)
#undef X
 },
 [LANG_DA]={
#define X(id,k,en,da) da,
 STR_TABLE(X)
#undef X
 }};
#if defined(MERAKIPLUS_PREVIEW)||defined(SIM_RT)
static const char*const STR_ID[NSTR]={
#define X(id,k,en,da) #id,
 STR_TABLE(X)
#undef X
};
#endif
#ifdef MERAKIPLUS_PREVIEW
static const unsigned char STR_K[NSTR]={
#define X(id,k,en,da) k,
 STR_TABLE(X)
#undef X
};
#endif
static int ui_lang=LANG_EN;
/* lookup per render: never cached across a language switch. Unknown id/language -> English. */
static const char* TR(int id){
    if(id<0||id>=NSTR) id=TX_NONE;
    const char*s=STR[(ui_lang>=0&&ui_lang<NLANG)?ui_lang:LANG_EN][id];
    return s?s:STR[LANG_EN][id];
}

/* ---- the language file: merakiplus's OWN setting (not the stock UI's config.language) ----
 * Content: exactly "en" or "da" (plain text; on read, whitespace/newlines and
 * upper case are tolerated, so `echo da > lang` also works). If the file is missing, cannot be read,
 * or contains anything else: ENGLISH. Written atomically: tmp in the same directory + fsync +
 * re-read/compare + rename() + fsync of the directory. */
static char lang_path[256]="/new_customer/merakiplus/lang";
static const char*const LANG_CODE[NLANG]={"en","da"};
enum { LS_DEFAULT=0, LS_FILE, LS_UNKNOWN, LS_SESSION };   /* where the active language comes from (DIAGNOSTICS) */
static int lang_src=LS_DEFAULT;
static int static_dirty;                                   /* (defined in the compositor section) */
static int lang_parse(const char*b,int n){
    while(n>0&&isspace((unsigned char)b[n-1])) n--;
    while(n>0&&isspace((unsigned char)*b)){ b++; n--; }
    if(n!=2) return -1;
    for(int l=0;l<NLANG;l++) if(tolower((unsigned char)b[0])==LANG_CODE[l][0]&&tolower((unsigned char)b[1])==LANG_CODE[l][1]) return l;
    return -1;
}
static void lang_set(int l){ if(l<0||l>=NLANG) l=LANG_EN; if(l!=ui_lang){ ui_lang=l; static_dirty=1; } }
static int lang_load(void){
    char b[16]; int l=-1, fd=open(lang_path,O_RDONLY);
    if(fd<0) lang_src=LS_DEFAULT;
    else {
        int n=(int)read(fd,b,sizeof b); close(fd);
        if(n<0) lang_src=LS_DEFAULT;                       /* unreadable (e.g. a directory) */
        else { l=(n>0&&n<(int)sizeof b)?lang_parse(b,n):-1; lang_src=l<0?LS_UNKNOWN:LS_FILE; }
    }
    lang_set(l<0?LANG_EN:l); return ui_lang;
}
/* returns 1 = the file now contains the language; 0 = nothing changed on disk (the old file is intact) */
static int lang_save(int l){
    if(l<0||l>=NLANG) return 0;
    char tmp[280],chk[8]; const char*d=LANG_CODE[l]; int n=(int)strlen(d);
    snprintf(tmp,sizeof tmp,"%s.tmp",lang_path);
    if(!write_all(tmp,d,n,0)){ unlink(tmp); return 0; }
    int fd=open(tmp,O_RDONLY); int cn=fd>=0?(int)read(fd,chk,sizeof chk):-1; if(fd>=0) close(fd);
    if(cn!=n||memcmp(chk,d,n)){ unlink(tmp); return 0; }
    if(rename(tmp,lang_path)!=0){ unlink(tmp); return 0; }
    fsync_dir(lang_path); lang_src=LS_FILE; return 1;
}

