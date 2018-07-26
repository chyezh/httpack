#include "event_timer_queue.h"
#include "event_loop.h"
#include "channel.h"
#include <cstring>

CYZPP_BEGIN

EventTimerQueue::EventTimerQueue(EventLoop *loop) 
  : owner_event_loop_(loop) {
  timerfd_ = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if(timerfd_ < 0) {
      ERRLOG << "Event timer construct fail\n";
      std::terminate();
  }
  // make new channel for this event queue
  timer_channel_ = std::make_unique<Channel>(timerfd_,loop);
  // set read call back
  timer_channel_->setReadCallback([this](){ runner(); });
  // enable the read
  timer_channel_->enableRead(true);
}

// add new event timer
void EventTimerQueue::addEventTimer(const EventTimer &timer) {
  owner_event_loop_->assertIsLoopingThread();
  // generate unique id for this timer
  EventTimerID timer_id = EventTimer::generateID();
  // emplace the new timer to the map
  event_timer_map_.emplace(std::make_pair(timer.getExpiration(), timer_id), timer);
  // if new timer has most higher priority, update timerfd
  if(event_timer_map_.begin()->first.second == timer_id)
    updateTimerFD(timer.getExpiration());
}

// run the expired event
void EventTimerQueue::runner() {
  // run the expired event
  auto expired_event_list = getExpiredTimer();
  for(auto &&event : expired_event_list)
    event.run();
  // if some event is pending, update the timerfd
  if(!event_timer_map_.empty())
    updateTimerFD(event_timer_map_.begin()->first.first);
}

// update the timerfd
void EventTimerQueue::updateTimerFD(TimePoint new_time) {
  struct itimerspec itime;
  bzero(&itime, sizeof(itimerspec));
  auto high_resolution_duration = new_time.time_since_epoch();
  std::chrono::seconds duration_sec = std::chrono::duration_cast<std::chrono::seconds>(high_resolution_duration);
  std::chrono::nanoseconds duration_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(high_resolution_duration - duration_sec);
  itime.it_value.tv_sec = duration_sec.count();
  itime.it_value.tv_nsec = duration_nsec.count();
  // update timerfd
  ::timerfd_settime(timerfd_, TFD_TIMER_ABSTIME, &itime, nullptr);
}

// obtain the expired event list
std::vector<EventTimer> EventTimerQueue::getExpiredTimer() {
  // find  all expired timer
  TimePoint now = Clock::now();
  auto iter_not_expired = event_timer_map_.upper_bound(std::make_pair(now, std::numeric_limits<EventTimerID>::min()));
  std::vector<EventTimer> expired_event;
  for(auto iter = event_timer_map_.begin(); iter != iter_not_expired; ++iter) {
    expired_event.emplace_back(iter->second);
  }
  event_timer_map_.erase(event_timer_map_.begin(), iter_not_expired);
  return expired_event;
}

CYZPP_END