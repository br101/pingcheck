/* pingcheck - Check connectivity of interfaces in OpenWRT
 *
 * Copyright (C) 2015 Bruno Randolf <br1@einfach.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include "log.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* run queue for scripts */
static struct runqueue runq;

/* for panic scripts */
static struct runqueue_process proc_panic;

/*
 * Here we fork and run the scripts in the child process.
 * The parent process just monitors the child process.
 */
static void task_scripts_run(struct runqueue* q, struct runqueue_task* t)
{
	char cmd[500];
	int len = 0;
	struct scripts_proc* scr = container_of(t, struct scripts_proc, proc.task);
	struct ping_intf* pi = scr->intf;
	char* state_str = (scr->state == ONLINE ? "online" : "offline");

	pid_t pid = fork();
	if (pid < 0) {
		LOG_ERR("Run scripts fork failed!");
		return;
	} else if (pid > 0) {
		/* parent process: monitor until child has finished */
		runqueue_process_add(q, &scr->proc, pid);
		return;
	}

	/* child process */
	len = snprintf(
		cmd, sizeof(cmd),
		"export INTERFACE=\"%s\"; export DEVICE=\"%s\"; export GLOBAL=\"%s\"; "
		"for hook in /etc/pingcheck/%s.d/*; do [ -r \"$hook\" ] && sh $hook; "
		"done",
		pi->name, pi->device, get_status_str(get_global_status()), state_str);

	if (len <= 0 || (unsigned int)len >= sizeof(cmd)) { // error or truncated
		LOG_ERR("Run scripts commands truncated!");
		_exit(EXIT_FAILURE);
	}

	LOG_NOTI("Running '%s' scripts for '%s'", state_str, pi->name);
	int ret = execlp("/bin/sh", "/bin/sh", "-c", cmd, NULL);
	if (ret == -1) {
		LOG_ERR("Run scripts exec error!");
		_exit(EXIT_FAILURE);
	}
}

/* runqueue callback when task got cancelled */
static void task_scripts_cancel(struct runqueue* q, struct runqueue_task* t,
								int type)
{
	struct scripts_proc* scr = container_of(t, struct scripts_proc, proc.task);
	LOG_NOTI("'%s' scripts for '%s' cancelled",
			 scr->state == ONLINE ? "online" : "offline", scr->intf->name);
	runqueue_process_cancel_cb(q, t, type);
}

/* runqueue callback when task got killed */
static void task_scripts_kill(struct runqueue* q, struct runqueue_task* t)
{
	struct scripts_proc* scr = container_of(t, struct scripts_proc, proc.task);
	LOG_NOTI("'%s' scripts for '%s' killed",
			 scr->state == ONLINE ? "online" : "offline", scr->intf->name);
	runqueue_process_kill_cb(q, t);
}

static const struct runqueue_task_type task_scripts_type
	= {.run = task_scripts_run,
	   .cancel = task_scripts_cancel,
	   .kill = task_scripts_kill};

/* called by main to request scripts to be run */
void scripts_run(struct ping_intf* pi, enum online_state state_new)
{
	struct scripts_proc* scr;
	struct scripts_proc* scr_other;
	const char* state_str;

	if (state_new == ONLINE) {
		scr = &pi->scripts_on;
		scr_other = &pi->scripts_off;
		state_str = "online";
	} else {
		scr = &pi->scripts_off;
		scr_other = &pi->scripts_on;
		state_str = "offline";
	}

	/*
	 * cancel obsolete other task: e.g when ONLINE script is getting queued
	 * and OFFLINE script is already queued and not running yet, cancel it
	 */
	if (scr_other->proc.task.queued && !scr_other->proc.task.running) {
		LOG_NOTI("Cancelling obsolete '%s' scripts for '%s'",
				 scr->state != ONLINE ? "online" : "offline", pi->name);
		runqueue_task_cancel(&scr_other->proc.task, 1);
	}

	/* don't queue the same scripts twice */
	if (scr->proc.task.queued || scr->proc.task.running) {
		LOG_NOTI("'%s' scripts for '%s' already queued or running", state_str,
				 pi->name);
		return;
	}

	/* add runqueue task for running the scripts */
	LOG_NOTI("Scheduling '%s' scripts for '%s'", state_str, pi->name);
	scr->proc.task.type = &task_scripts_type;
	scr->proc.task.run_timeout = SCRIPTS_TIMEOUT * 1000;
	scr->intf = pi;
	scr->state = state_new;
	runqueue_task_add(&runq, &scr->proc.task, false);
}

static void task_panic_run(struct runqueue* q, struct runqueue_task* t)
{
	pid_t pid = fork();
	if (pid < 0) {
		LOG_ERR("Run scripts fork failed!");
		return;
	} else if (pid > 0) {
		/* parent process: monitor until child has finished */
		struct runqueue_process* rp = (struct runqueue_process*)t;
		runqueue_process_add(q, rp, pid);
		return;
	}

	/* child process */
	char cmd[500];
	int len = snprintf(cmd, sizeof(cmd),
					   "for hook in /etc/pingcheck/panic.d/*; do [ -r "
					   "\"$hook\" ] && sh $hook; done");

	if (len <= 0 || (unsigned int)len >= sizeof(cmd)) { // error or truncated
		LOG_ERR("Run scripts commands truncated!");
		_exit(EXIT_FAILURE);
	}

	LOG_NOTI("Running PANIC scripts");
	int ret = execlp("/bin/sh", "/bin/sh", "-c", cmd, NULL);
	if (ret == -1) {
		LOG_ERR("Run scripts exec error!");
		_exit(EXIT_FAILURE);
	}
}

static const struct runqueue_task_type task_scripts_panic_type = {
	.run = task_panic_run,
};

void scripts_run_panic()
{
	LOG_NOTI("Scheduling PANIC scripts");
	proc_panic.task.type = &task_scripts_panic_type;
	proc_panic.task.run_timeout = SCRIPTS_TIMEOUT * 1000;
	runqueue_task_add(&runq, &proc_panic.task, false);
}

void scripts_init(void)
{
	runqueue_init(&runq);
	runq.max_running_tasks = 1;
}

void scripts_finish(void)
{
	runqueue_kill(&runq);
}
