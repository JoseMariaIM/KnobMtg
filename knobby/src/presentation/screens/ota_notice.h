#ifndef _OTA_NOTICE_H
#define _OTA_NOTICE_H

/* The two firmware-update banners, and when they fire.
 *
 * "An update is available" the first time the OTA check sees one, and
 * "just updated to X" once after a boot whose version differs from the
 * one stored at the previous boot. Both are application decisions
 * about a feature - which is why they no longer live in hw.c, where
 * they made the hardware layer depend on wifi_ota and on the UI it
 * navigates to. */

#ifdef __cplusplus
extern "C" {
#endif

/* Call once at boot, after screens are built. Shows the "just updated"
   toast if the running version differs from the stored one, and
   records the running version either way. */
void ota_notice_check_after_boot(void);

/* One cheap poll of the OTA state. Registered with
   hw_set_idle_poll_hook() at boot rather than given a timer of its
   own - see the comment there. */
void ota_notice_poll(void);

#ifdef __cplusplus
}
#endif

#endif // _OTA_NOTICE_H
