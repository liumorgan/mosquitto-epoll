/*
Copyright (c) 2009-2014 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License v1.0
and Eclipse Distribution License v1.0 which accompany this distribution.
 
The Eclipse Public License is available at
   http://www.eclipse.org/legal/epl-v10.html
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.
 
Contributors:
   Roger Light - initial implementation and documentation.
*/
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifndef WIN32
#include <syslog.h>
#endif
#include <time.h>

#ifndef CMAKE
#include <config.h>
#endif

#include <mosquitto_broker.h>
#include <memory_mosq.h>

extern struct mosquitto_db int_db;

#ifdef WIN32
HANDLE syslog_h;
#endif

/* Options for logging should be:
 *
 * A combination of:
 * Via syslog
 * To a file
 * To stdout/stderr
 * To topics
 */

/* Give option of logging timestamp.
 * Logging pid.
 */
static int log_destinations = MQTT3_LOG_STDERR;
static int log_priorities = MOSQ_LOG_ERR | MOSQ_LOG_WARNING | MOSQ_LOG_NOTICE | MOSQ_LOG_INFO;

int mqtt3_log_init(int priorities, int destinations, int facility)
{
	int rc = 0;

	log_priorities = priorities;
	log_destinations = destinations;

	if(log_destinations & MQTT3_LOG_SYSLOG){
#ifndef WIN32
		openlog("mosquitto", LOG_PID|LOG_CONS, facility);
#else
		syslog_h = OpenEventLog(NULL, "mosquitto");
#endif
	}

	return rc;
}

int mqtt3_log_close(void)
{
	if(log_destinations & MQTT3_LOG_SYSLOG){
#ifndef WIN32
		closelog();
#else
		CloseEventLog(syslog_h);
#endif
	}
	/* FIXME - do something for all destinations! */

	return MOSQ_ERR_SUCCESS;
}

int _mosquitto_log_vprintf(struct mosquitto *mosq, int priority, const char *fmt, va_list va)
{
	char *s;
	char *st;
	int len;
#ifdef WIN32
	char *sp;
#endif
	const char *topic;
	int syslog_priority;
	time_t now = time(NULL);

	if((log_priorities & priority) && log_destinations != MQTT3_LOG_NONE){
		switch(priority){
			case MOSQ_LOG_SUBSCRIBE:
				topic = "$SYS/broker/log/M/subscribe";
#ifndef WIN32
				syslog_priority = LOG_NOTICE;
#else
				syslog_priority = EVENTLOG_INFORMATION_TYPE;
#endif
				break;
			case MOSQ_LOG_UNSUBSCRIBE:
				topic = "$SYS/broker/log/M/unsubscribe";
#ifndef WIN32
				syslog_priority = LOG_NOTICE;
#else
				syslog_priority = EVENTLOG_INFORMATION_TYPE;
#endif
				break;
			case MOSQ_LOG_DEBUG:
				topic = "$SYS/broker/log/D";
#ifndef WIN32
				syslog_priority = LOG_DEBUG;
#else
				syslog_priority = EVENTLOG_INFORMATION_TYPE;
#endif
				break;
			case MOSQ_LOG_ERR:
				topic = "$SYS/broker/log/E";
#ifndef WIN32
				syslog_priority = LOG_ERR;
#else
				syslog_priority = EVENTLOG_ERROR_TYPE;
#endif
				break;
			case MOSQ_LOG_WARNING:
				topic = "$SYS/broker/log/W";
#ifndef WIN32
				syslog_priority = LOG_WARNING;
#else
				syslog_priority = EVENTLOG_WARNING_TYPE;
#endif
				break;
			case MOSQ_LOG_NOTICE:
				topic = "$SYS/broker/log/N";
#ifndef WIN32
				syslog_priority = LOG_NOTICE;
#else
				syslog_priority = EVENTLOG_INFORMATION_TYPE;
#endif
				break;
			case MOSQ_LOG_INFO:
				topic = "$SYS/broker/log/I";
#ifndef WIN32
				syslog_priority = LOG_INFO;
#else
				syslog_priority = EVENTLOG_INFORMATION_TYPE;
#endif
				break;
			default:
				topic = "$SYS/broker/log/E";
#ifndef WIN32
				syslog_priority = LOG_ERR;
#else
				syslog_priority = EVENTLOG_ERROR_TYPE;
#endif
		}
		len = strlen(fmt) + 500;
		s = _mosquitto_malloc(len*sizeof(char));
		if(!s) return MOSQ_ERR_NOMEM;

		vsnprintf(s, len, fmt, va);
		s[len-1] = '\0'; /* Ensure string is null terminated. */

		if(log_destinations & MQTT3_LOG_STDOUT){
			if(int_db.config && int_db.config->log_timestamp){
				fprintf(stdout, "%d: %s\n", (int)now, s);
			}else{
				fprintf(stdout, "%s\n", s);
			}
			fflush(stdout);
		}
		if(log_destinations & MQTT3_LOG_STDERR){
			if(int_db.config && int_db.config->log_timestamp){
				fprintf(stderr, "%d: %s\n", (int)now, s);
			}else{
				fprintf(stderr, "%s\n", s);
			}
			fflush(stderr);
		}
		if(log_destinations & MQTT3_LOG_FILE && int_db.config->log_fptr){
			if(int_db.config && int_db.config->log_timestamp){
				fprintf(int_db.config->log_fptr, "%d: %s\n", (int)now, s);
			}else{
				fprintf(int_db.config->log_fptr, "%s\n", s);
			}
		}
		if(log_destinations & MQTT3_LOG_SYSLOG){
#ifndef WIN32
			syslog(syslog_priority, "%s", s);
#else
			sp = (char *)s;
			ReportEvent(syslog_h, syslog_priority, 0, 0, NULL, 1, 0, &sp, NULL);
#endif
		}
		if(log_destinations & MQTT3_LOG_TOPIC && priority != MOSQ_LOG_DEBUG){
			if(int_db.config && int_db.config->log_timestamp){
				len += 30;
				st = _mosquitto_malloc(len*sizeof(char));
				if(!st){
					_mosquitto_free(s);
					return MOSQ_ERR_NOMEM;
				}
				snprintf(st, len, "%d: %s", (int)now, s);
				mqtt3_db_messages_easy_queue(&int_db, NULL, topic, 2, strlen(st), st, 0);
				_mosquitto_free(st);
			}else{
				mqtt3_db_messages_easy_queue(&int_db, NULL, topic, 2, strlen(s), s, 0);
			}
		}
		_mosquitto_free(s);
	}

	return MOSQ_ERR_SUCCESS;
}

int _mosquitto_log_printf(struct mosquitto *mosq, int priority, const char *fmt, ...)
{
	va_list va;
	int rc;

	va_start(va, fmt);
	rc = _mosquitto_log_vprintf(mosq, priority, fmt, va);
	va_end(va);

	return rc;
}

void mosquitto_log_printf(int level, const char *fmt, ...)
{
	va_list va;

	va_start(va, fmt);
	_mosquitto_log_vprintf(NULL, level, fmt, va);
	va_end(va);
}

