#ifndef _LIBAVAUDIOSINK_H_
#define _LIBAVAUDIOSINK_H_

#include <stdint.h>

typedef struct OpaqueAVAudioSinkSession AVAudioSinkSessionRef;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
  AVAudioSinkSessionRef* avaudiosink_open(const int framebytes);
  void avaudiosink_close(AVAudioSinkSessionRef *avref);
  int avaudiosink_write(AVAudioSinkSessionRef* avref, const uint8_t *buf, int len);
  void avaudiosink_play(AVAudioSinkSessionRef* avref, const int playpause);
  void avaudiosink_flush(AVAudioSinkSessionRef* avref);
  int  avaudiosink_ready(AVAudioSinkSessionRef* avref);
  double avaudiosink_timeseconds(AVAudioSinkSessionRef* avref);
  double avaudiosink_delayseconds(AVAudioSinkSessionRef* avref);
  double avaudiosink_delay2seconds(AVAudioSinkSessionRef* avref);
  double avaudiosink_errorseconds(AVAudioSinkSessionRef* avref);
  double avaudiosink_mindelayseconds(AVAudioSinkSessionRef* avref);
  double avaudiosink_maxdelayseconds(AVAudioSinkSessionRef* avref);
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
