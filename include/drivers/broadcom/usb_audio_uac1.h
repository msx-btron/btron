/*
 * BTRON3 USB Audio Class 1.0 (UAC1) Driver Header
 * Isochronous PCM streaming over xHCI for Raspberry Pi 400 & QEMU
 * Conforming to B-Audio specification & NASA JPL Rules
 */

#ifndef _BTRON_USB_AUDIO_UAC1_H_
#define _BTRON_USB_AUDIO_UAC1_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* USB Audio Class Constants */
#define USB_CLASS_AUDIO                0x01
#define USB_SUBCLASS_AUDIOCONTROL      0x01
#define USB_SUBCLASS_AUDIOSTREAMING    0x02
#define USB_SUBCLASS_MIDISTREAMING     0x03

/* Audio Class-Specific Descriptor Types */
#define UAC_CS_DEVICE                  0x21
#define UAC_CS_CONFIGURATION           0x22
#define UAC_CS_STRING                  0x23
#define UAC_CS_INTERFACE               0x24
#define UAC_CS_ENDPOINT                0x25

/* Audio Class-Specific Request Codes */
#define UAC_REQ_SET_CUR                0x01
#define UAC_REQ_GET_CUR                0x81
#define UAC_REQ_SET_MIN                0x02
#define UAC_REQ_GET_MIN                0x82
#define UAC_REQ_SET_MAX                0x03
#define UAC_REQ_GET_MAX                0x83
#define UAC_REQ_SET_RES                0x04
#define UAC_REQ_GET_RES                0x84

/* Audio Endpoint Controls */
#define UAC_EP_CS_SAMPLING_FREQ_CONTROL 0x01

/* UAC1 PCM Parameters */
#define UAC1_DEFAULT_RATE              48000u
#define UAC1_DEFAULT_CHANNELS          2u
#define UAC1_FRAME_SAMPLES_1MS         48u   /* 48 samples per 1 ms frame @ 48 kHz */
#define UAC1_FRAME_BYTES_1MS           (UAC1_FRAME_SAMPLES_1MS * UAC1_DEFAULT_CHANNELS * sizeof(int16_t)) /* 192 bytes */
#define UAC1_RING_PACKETS              16u   /* 16 ms total queue depth */
#define UAC1_TARGET_LATENCY_PACKETS    4u    /* 4 ms target real-time latency */

/* Public API Functions */
void     usb_audio_uac1_init(void);
void     usb_audio_uac1_attach(uint32_t slot_id, uint8_t ep_addr, uint32_t max_packet_size);
/* Speaker reached through the DWC2 host (QEMU raspi4b usb-audio). */
void     usb_audio_uac1_attach_dwc2(uint8_t ep_addr, uint32_t max_packet_size);
void     usb_audio_uac1_detach(uint32_t slot_id);
bool     usb_audio_uac1_is_present(void);
void     usb_audio_uac1_on_transfer_complete(void);
const int16_t *usb_audio_uac1_get_packet_buffer(uint32_t packet_idx);

/* B-Audio Standard 5-Call Driver Interface */
int      usb_audio_uac1_open(uint32_t sample_rate, uint8_t channels);
int      usb_audio_uac1_write(const int16_t *samples, size_t frames);
void     usb_audio_uac1_close(void);
bool     usb_audio_uac1_is_ready(void);
uint32_t usb_audio_uac1_get_queued_bytes(void);

#endif /* _BTRON_USB_AUDIO_UAC1_H_ */
