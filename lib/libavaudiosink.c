#include <stdlib.h>

#include "AERingBuffer.h"

#import <AVFoundation/AVFoundation.h>
#import <AVFoundation/AVAudioSession.h>

#pragma mark - AudioResourceLoader
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
@interface AudioResourceLoader : NSObject <AVAssetResourceLoaderDelegate>
@property (nonatomic) bool abortflag;
@property (nonatomic) unsigned int frameBytes;
@property (nonatomic) char *readbuffer;
@property (nonatomic) size_t readbufferSize;
@property (nonatomic) AERingBuffer *avbuffer;
@property (nonatomic) int errorCount;
@property (nonatomic) unsigned int transferCount;
@property (nonatomic) NSData *dataAtOffsetZero;
- (id)initWithFrameBytes:(unsigned int)frameBytes;
- (void)abort;
- (int) write:(uint8_t*)data size:(unsigned int)size;
- (double)errorSeconds;
- (double)bufferSeconds;
- (double)transferSeconds;
@end

@implementation AudioResourceLoader
- (id)initWithFrameBytes:(unsigned int)frameBytes
{
  self = [super init];
  if (self)
  {
    _abortflag = false;
    _frameBytes = frameBytes;
    // readbufferSize must be greater that 65536.
    _readbufferSize = _frameBytes * 48;
    _avbuffer = new AERingBuffer(_readbufferSize);
    // make readbuffer one frame larger
    _readbufferSize += _frameBytes;
    _readbuffer = new char[_readbufferSize];
    _errorCount = 0;
    _transferCount = 0;
  }
  return self;
}

- (void)dealloc
{
  delete _avbuffer, _avbuffer = nullptr;
  delete _readbuffer, _avbuffer = nullptr;
}

- (NSError *)loaderCancelledError
{
  NSError *error = [[NSError alloc] initWithDomain:@"AudioResourceLoaderErrorDomain"
    code:-1 userInfo:@{NSLocalizedDescriptionKey:@"avloader cancelled"}];
  _abortflag = true;
  return error;
}

#define logDataRequestError 0
#define logDataRequestEndOf 0
#define logDataRequestBgnEnd 0
#define logDataRequestSending 0
- (BOOL)resourceLoader:(AVAssetResourceLoader *)resourceLoader shouldWaitForLoadingOfRequestedResource:(AVAssetResourceLoadingRequest *)loadingRequest
{
  AVAssetResourceLoadingContentInformationRequest* contentRequest = loadingRequest.contentInformationRequest;

  if (_abortflag)
  {
    [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
    return YES;
  }

  if (contentRequest)
  {
    // only handles eac3
    contentRequest.contentType = @"public.enhanced-ac3-audio";
    // (2147483647) or 383479.222678571428571 seconds at 0.032 secs pre 1792 byte frame
    // or 10.652200629960317 hours.
    contentRequest.contentLength = INT_MAX;
    // must be 'NO' to get player to start playing immediately
    contentRequest.byteRangeAccessSupported = NO;
    //NSLog(@"avloader contentRequest %@", contentRequest);
  }

  AVAssetResourceLoadingDataRequest* dataRequest = loadingRequest.dataRequest;
  if (dataRequest)
  {
    //There where always 3 initial requests
    // 1) one for the first two bytes of the file
    // 2) one from the beginning of the file
    // 3) one from the end of the file
    //NSLog(@"resourceLoader dataRequest %@", dataRequest);
#if logDataRequestBgnEnd
    NSLog(@"avloader dataRequest bgn");
#endif
    NSInteger reqLen = dataRequest.requestedLength;
    if (reqLen == 2)
    {
      // 1) from above.
      // avplayer always 1st read two bytes to check for a content tag.
      // ac3/eac3 has two byte tag of 0x0b77, \v is vertical tab == 0x0b
      [dataRequest respondWithData:[NSData dataWithBytes:"\vw" length:2]];
      [loadingRequest finishLoading];
      //NSLog(@"avloader check content tag, %u in buffer", _avbuffer->GetReadSize());
    }
    else
    {
      size_t requestedBytes = _frameBytes * 2;
      if (dataRequest.requestedOffset == 0)
      {
        // 2) above. make sure avplayer has enough frame blocks at startup
        requestedBytes = _frameBytes * 36;
      }

      if (dataRequest.requestsAllDataToEndOfResource == NO && dataRequest.requestedOffset != 0)
      {
        // 3) from above.
        // we have already hit 2) and saved it.
        // just shove it back to make avplayer happy.
        [dataRequest respondWithData:_dataAtOffsetZero];
#if logDataRequestEndOf
        NSLog(@"avloader check endof, %u in buffer", _avbuffer->GetReadSize());
#endif
        //NSLog(@"avloader requestedLength:%zu, requestedOffset:%lld, currentOffset:%lld, bufferbytes:%u",
        //  dataRequest.requestedLength, dataRequest.requestedOffset, dataRequest.currentOffset, _avbuffer->GetReadSize());
        [loadingRequest finishLoading];
#if logDataRequestBgnEnd
        NSLog(@"avloader dataRequest end");
#endif
        return YES;
      }

      // 2) from above and any other type of transfer
      while (_avbuffer->GetReadSize() < requestedBytes)
      {
        if (_abortflag)
        {
          [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
#if logDataRequestBgnEnd
          NSLog(@"avloader dataRequest end");
#endif
          return YES;
        }
        usleep(10 * 1000);
      }
      if (dataRequest.requestsAllDataToEndOfResource == YES && dataRequest.requestedLength > (long)requestedBytes)
      {
        // calc how many complete frames are present
        size_t maxFrameBytes = _frameBytes * (_avbuffer->GetReadSize() / _frameBytes);
        // limit to size of _readbuffer
        if (maxFrameBytes > (_readbufferSize))
          maxFrameBytes = _frameBytes * ((_readbufferSize) / _frameBytes);
        //NSLog(@"avloader maxframebytes %lu", maxFrameBytes);
        requestedBytes = maxFrameBytes;
      }

      _avbuffer->Read((unsigned char*)_readbuffer, requestedBytes);

      // check if we have enough data
      if (requestedBytes)
      {
        NSData *data = [NSData dataWithBytes:_readbuffer length:requestedBytes];
        if (dataRequest.requestsAllDataToEndOfResource == NO && dataRequest.requestedOffset == 0)
          _dataAtOffsetZero = [NSData dataWithBytes:_readbuffer length:requestedBytes];
        [dataRequest respondWithData:data];

#if logDataRequestSending
        // log the transfer
        size_t bufferbytes = _avbuffer->GetReadSize();
        if (bufferbytes > 0)
          NSLog(@"avloader sending %lu bytes, %zu in buffer",
            (unsigned long)[data length], bufferbytes);
        else
          NSLog(@"avloader sending %lu bytes", (unsigned long)[data length]);
#endif
        _transferCount += requestedBytes;
        if (_transferCount != dataRequest.currentOffset)
        {
          _errorCount = _transferCount - dataRequest.currentOffset;
#if logDataRequestError
          CLog::Log(LOGWARNING, "avloader requestedLength:%zu, requestedOffset:%lld, currentOffset:%lld, _transferCount:%u, bufferbytes:%u",
            dataRequest.requestedLength, dataRequest.requestedOffset, dataRequest.currentOffset, _transferCount, _avbuffer->GetReadSize());
#endif
        }
        [loadingRequest finishLoading];
      }
      else
      {
#if logDataRequestError
        NSLog(@"avloader loaderCancelledError");
#endif
        [loadingRequest finishLoadingWithError:[self loaderCancelledError]];
      }
    }
  }
#if logDataRequestBgnEnd
  NSLog(@"avloader dataRequest end");
#endif

  return YES;
}

- (void)resourceLoader:(AVAssetResourceLoader *)resourceLoader
  didCancelLoadingRequest:(AVAssetResourceLoadingRequest *)loadingRequest
{
  _abortflag = true;
#if logDataRequestError
  NSLog(@"avloader didCancelLoadingRequest");
#endif
}

- (void)abort
{
  _abortflag = true;
}

- (int)write:(uint8_t*)data size:(unsigned int)size
{
  if (data && size > 0)
  {
    int ok = AE_RING_BUFFER_OK;
    if (_avbuffer->Write(data, size) == ok)
      return size;
  }

  return 0;
}

- (double)errorSeconds
{
  return ((double)_errorCount / _frameBytes) * 0.032;
}

- (double)bufferSeconds
{
  return ((double)_avbuffer->GetReadSize() / _frameBytes) * 0.032;
}

- (double)transferSeconds
{
  return ((double)_transferCount / _frameBytes) * 0.032;
}
@end

#pragma mark - AVPlayerSink
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
@interface AVPlayerSink : NSObject
- (id)initWithFrameSize:(unsigned int)frameSize;
- (void)close;
- (int)addPackets:(uint8_t*)data size:(unsigned int)size;
- (void)play:(bool) state;
- (void)start;
- (bool)loaded;
- (void)flush;
- (double)errorSeconds;
- (double)clockSeconds;
- (double)bufferSeconds;
- (double)sinkBufferSeconds;
@end

@interface AVPlayerSink ()
@property (nonatomic) bool loadedFlag;
@property (nonatomic) dispatch_queue_t serialQueue;
@property (nonatomic) AVPlayer *avplayer;
@property (nonatomic) AVPlayerItem *playerItem;
@property (nonatomic) unsigned int frameSize;
@property (nonatomic) NSString *contentType;
@property (nonatomic) AudioResourceLoader *avLoader;
@end

@implementation AVPlayerSink
//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
- (id)initWithFrameSize:(unsigned int)frameSize;
{
  self = [super init];
  if (self)
  {
    _loadedFlag = false;
    _frameSize = frameSize;
    _serialQueue = dispatch_queue_create("com.mrmc.loaderqueue", DISPATCH_QUEUE_SERIAL);

    _avplayer = [[AVPlayer alloc] init];
    // this little gem primes avplayer so next
    // load with a playerItem is fast... go figure.
    NSBundle *bundle = [NSBundle bundleForClass:[self class]];
    if (bundle)
    {
      NSString *filepath = [bundle pathForResource:@"point1sec" ofType:@"mp3"];
      if (filepath)
      {
        //NSLog(@"avloader playing silence =  %@", filepath);
        AVPlayerItem *playerItem = [AVPlayerItem playerItemWithURL:[NSURL fileURLWithPath:filepath]];
        [_avplayer replaceCurrentItemWithPlayerItem:playerItem];
        [_avplayer play];
      }
    }
  }
  NSString *name = [NSProcessInfo processInfo].processName;
  if (![name hasPrefix:[NSString stringWithUTF8String:"MrMC"]])
  {
    //NSLog(@"avloader processName =  %@", name);
    return nil;
  }

  return self;
}

//-----------------------------------------------------------------------------------
- (void)close
{
  [_avplayer.currentItem.asset cancelLoading];
  [_avplayer replaceCurrentItemWithPlayerItem:nil];
  _avplayer = nullptr;
  [_playerItem removeObserver:self forKeyPath:@"status"];
  [_playerItem.asset cancelLoading];
  _playerItem = nullptr;
  [_avLoader abort];
  _avLoader = nullptr;
}

//-----------------------------------------------------------------------------------
- (void)dealloc
{
  [self close];
}

//-----------------------------------------------------------------------------------
- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context
{
  if (object == _playerItem && [keyPath isEqualToString:@"status"])
  {
      if (_avplayer.currentItem.status == AVPlayerItemStatusReadyToPlay)
      {
        if (abs([_avplayer rate]) > 0.0)
          _loadedFlag = true;
        else
        {
          // we can only preroll if not playing (rate == 0.0)
          [_avplayer prerollAtRate:2.0 completionHandler:^(BOOL finished)
          {
            // set loaded regardless of finished or not.
            _loadedFlag = true;
          }];
        }
        //NSLog(@"avloader AVPlayerItemStatusReadyToPlay loaded %d", _loadedFlag);
      }
  }
  else
  {
    [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
  }
}

//-----------------------------------------------------------------------------------
- (int)addPackets:(uint8_t*)data size:(unsigned int)size
{
  if (data && size > 0)
    return [_avLoader write:data size:size];

  return size;
}

//-----------------------------------------------------------------------------------
- (void)play:(bool)state
{
  if (state)
    [_avplayer playImmediatelyAtRate:1.0];
  else
    [_avplayer pause];
}

//-----------------------------------------------------------------------------------
- (void)start
{
/*
  for (NSString *mime in [AVURLAsset audiovisualTypes])
    NSLog(@"AVURLAsset audiovisualTypes:%@", mime);

  for (NSString *mime in [AVURLAsset audiovisualMIMETypes])
    NSLog(@"AVURLAsset audiovisualMIMETypes:%@", mime);
*/
  // run on our own serial queue, keeps main thread from stalling us
  // and lets us do long sleeps waiting for data without stalling main thread.
  dispatch_sync(_serialQueue, ^{
    NSString *extension = @"ec3";
    // needs leading dir ('fake') or pathExtension in resourceLoader will fail
    NSMutableString *url = [NSMutableString stringWithString:@"mrmc_streaming://fake/dummy."];
    [url appendString:extension];
    NSURL *streamURL = [NSURL URLWithString: url];
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:streamURL options:nil];
    _avLoader = [[AudioResourceLoader alloc] initWithFrameBytes:_frameSize];
    [asset.resourceLoader setDelegate:_avLoader queue:_serialQueue];

    _playerItem = [AVPlayerItem playerItemWithAsset:asset];
    [_playerItem addObserver:self forKeyPath:@"status" options:NSKeyValueObservingOptionNew context:nil];

    [_avplayer replaceCurrentItemWithPlayerItem:_playerItem];
    _avplayer.actionAtItemEnd = AVPlayerActionAtItemEndNone;
    _avplayer.automaticallyWaitsToMinimizeStalling = NO;
    _avplayer.currentItem.canUseNetworkResourcesForLiveStreamingWhilePaused = YES;
  });
}

//-----------------------------------------------------------------------------------
- (bool)loaded
{
  return _loadedFlag;
}

//-----------------------------------------------------------------------------------
- (void)flush
{
  [_avplayer replaceCurrentItemWithPlayerItem:nil];
  [_playerItem removeObserver:self forKeyPath:@"status"];
  [_playerItem.asset cancelLoading];
  _playerItem = nullptr;
  [_avLoader abort];
  _avLoader = nullptr;
  [self start];
}

//-----------------------------------------------------------------------------------
- (double)errorSeconds
{
  return [_avLoader errorSeconds];
}

//-----------------------------------------------------------------------------------
- (double)clockSeconds
{
  CMTime currentTime = [_playerItem currentTime];
  double sink_s = CMTimeGetSeconds(currentTime);
  // errorSeconds is a time chunk that was lost
  // during transfer to avplayer. Either avplayer ate
  // it or there was a buffer overlow somewhere...
  double errorSeconds = [_avLoader errorSeconds];
  sink_s += errorSeconds;
  if (sink_s > 0.0)
    return sink_s;
  else
    return 0.0;
}

//-----------------------------------------------------------------------------------
- (double)bufferSeconds
{
  double avbufferSeconds = [_avLoader bufferSeconds];
  double transferSeconds = [_avLoader transferSeconds];
  double playerSeconds = [self clockSeconds];
  double seconds = (transferSeconds - playerSeconds) + avbufferSeconds;
  if (seconds < 0.0)
    seconds = 0.0;

  return seconds;
}

//-----------------------------------------------------------------------------------
- (double)sinkBufferSeconds
{
  double buffered_s = 0.0;
  if (_playerItem)
  {
    NSArray *timeRanges = [_playerItem loadedTimeRanges];
    if (timeRanges && [timeRanges count])
    {
      CMTimeRange timerange = [[timeRanges objectAtIndex:0]CMTimeRangeValue];
      //NSLog(@"avloader timerange.start %f", CMTimeGetSeconds(timerange.start));
      double duration = CMTimeGetSeconds(timerange.duration);
      //NSLog(@"avloader timerange.duration %f", duration);
      buffered_s = duration - [self clockSeconds];
      if (buffered_s < 0.0)
        buffered_s = 0.0;
    }
  }
  return buffered_s;
}
@end

//-----------------------------------------------------------------------------------
//-----------------------------------------------------------------------------------
typedef struct OpaqueAVAudioSinkSession
{
  int frameSize;
  AVPlayerSink *avsink;
}
AVAudioSinkSessionRef;

extern "C" AVAudioSinkSessionRef* avaudiosink_open(const int framebytes)
{
  AVAudioSinkSessionRef *avref = NULL;
  avref = (AVAudioSinkSessionRef*)calloc(1, sizeof(AVAudioSinkSessionRef));
  avref->frameSize = framebytes;
  avref->avsink = [[AVPlayerSink alloc] initWithFrameSize:framebytes];
  if (avref->avsink)
    [avref->avsink start];
  else
    free(avref), avref = nullptr;
  return avref;
};

// close, going away
extern "C" void avaudiosink_close(AVAudioSinkSessionRef *avref)
{
  if (avref && avref->avsink)
  {
    [avref->avsink close];
    avref->avsink = nullptr;
  }
  free(avref);
};

// add frame packets to sink buffers
extern "C" int avaudiosink_write(AVAudioSinkSessionRef* avref, const uint8_t *buf, int len)
{
  int written = 0;
  if (avref && avref->avsink)
    written = [avref->avsink addPackets:(uint8_t*)buf size:len];
  return written;
};

// start/stop audio playback
extern "C" void avaudiosink_play(AVAudioSinkSessionRef* avref, const int playpause)
{
  if (avref && avref->avsink)
    [avref->avsink play:playpause];
};

// flush audio playback buffers
extern "C" void avaudiosink_flush(AVAudioSinkSessionRef* avref)
{
  if (avref && avref->avsink)
    [avref->avsink flush];
};

// return true when sink is ready to output audio after filling
extern "C" int avaudiosink_ready(AVAudioSinkSessionRef* avref)
{
  if (avref && avref->avsink)
    return [avref->avsink loaded] == true;
  return 0;
};

// time in seconds of when sound hits your ears
extern "C" double avaudiosink_timeseconds(AVAudioSinkSessionRef* avref)
{
  if (avref && avref->avsink)
    return [avref->avsink clockSeconds];
  return 0.0;
};

// delay in seconds of adding data before it hits your ears
extern "C" double avaudiosink_delayseconds(AVAudioSinkSessionRef* avref)
{
  if (avref && avref->avsink)
    return [avref->avsink bufferSeconds];
  return 0.0;
};

// alternative delay_s method
extern "C" double avaudiosink_delay2seconds(AVAudioSinkSessionRef* avref)
{
  if (avref && avref->avsink)
    return [avref->avsink sinkBufferSeconds];
  return 0.0;
};

// sink error seconds
extern "C" double avaudiosink_errorseconds(AVAudioSinkSessionRef* avref)
{
  if (avref && avref->avsink)
    return [avref->avsink errorSeconds];
  return 0.0;
};

extern "C" double avaudiosink_mindelayseconds(AVAudioSinkSessionRef* avref)
{
  return 2.5;
};

extern "C" double avaudiosink_maxdelayseconds(AVAudioSinkSessionRef* avref)
{
  return 3.0;
};

