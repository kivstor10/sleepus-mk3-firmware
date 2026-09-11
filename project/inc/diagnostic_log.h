#ifndef DIAGNOSTIC_LOG_H
#define DIAGNOSTIC_LOG_H

#include <stdint.h>

#ifdef USB_DIAGNOSTICS
void diagnostic_uart_init(void);
void diagnostic_uart_task(void);
void diagnostic_log_event(const char *event, uint32_t value_a,
                          uint32_t value_b, uint32_t value_c);
#ifdef USB_VERBOSE_DIAGNOSTICS
#define diagnostic_trace_event diagnostic_log_event
#else
#define diagnostic_trace_event(event, value_a, value_b, value_c) ((void)0)
#endif
#else
#define diagnostic_uart_init() ((void)0)
#define diagnostic_uart_task() ((void)0)
#define diagnostic_log_event(event, value_a, value_b, value_c) ((void)0)
#define diagnostic_trace_event(event, value_a, value_b, value_c) ((void)0)
#endif

#endif