#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <syslog.h>
#include "main.h"

/* run queue for scripts */
static struct runqueue runq;

/*
 * Here we fork and run the scripts in the child process.
 * The parent process just monitors the child process.
 */
static void task_scripts_run(struct runqueue *q, struct runqueue_task *t)
{
	char cmd[500];
	int len = 0;
	struct scripts_proc* scr = container_of(t, struct scripts_proc, proc.task);
	struct ping_intf* pi = scr->intf;
	char* state_str = (scr->state == ONLINE ? "online" : "offline");

	pid_t pid = fork();
	if (pid < 0) {
		printlog(LOG_ERR, "Run scripts fork failed!");
		return;
	} else if (pid > 0) {
		/* parent process: monitor until child has finished */
		runqueue_process_add(q, &scr->proc, pid);
		return;
	}

	/* child process */
	len = snprintf(cmd, sizeof(cmd),
		       "export INTERFACE=\"%s\"; export DEVICE=\"%s\"; "
		       "for hook in /etc/pingcheck/%s.d/*; do [ -r \"$hook\" ] && sh $hook; done",
		       pi->name, pi->device, state_str);

	if (len <= 0 || (unsigned int)len >= sizeof(cmd)) { // error or truncated
		printlog(LOG_ERR, "Run scripts commands truncated!");
		_exit(EXIT_FAILURE);
	}

	printlog(LOG_NOTICE, "Running '%s' scripts for '%s'", state_str, pi->name);
	int ret = execlp("/bin/sh", "/bin/sh", "-c", cmd, NULL);
	if (ret == -1) {
		printlog(LOG_ERR, "Run scripts exec error!");
		_exit(EXIT_FAILURE);
	}
}

/* runqueue callback when task got cancelled */
static void task_scripts_cancel(struct runqueue *q, struct runqueue_task *t, int type)
{
	struct scripts_proc* scr = container_of(t, struct scripts_proc, proc.task);
	printlog(LOG_NOTICE, "'%s' scripts for '%s' cancelled",
		 scr->state == ONLINE ? "online" : "offline", scr->intf->name);
	runqueue_process_cancel_cb(q, t, type);
}

/* runqueue callback when task got killed */
static void task_scripts_kill(struct runqueue *q, struct runqueue_task *t)
{
	struct scripts_proc* scr = container_of(t, struct scripts_proc, proc.task);
	printlog(LOG_NOTICE, "'%s' scripts for '%s' killed",
		 scr->state == ONLINE ? "online" : "offline", scr->intf->name);
	runqueue_process_kill_cb(q, t);
}

static const struct runqueue_task_type task_scripts_type = {
	.run = task_scripts_run,
	.cancel = task_scripts_cancel,
	.kill = task_scripts_kill
};

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
		printlog(LOG_NOTICE, "Cancelling obsolete '%s' scripts for '%s'",
			 scr->state != ONLINE ? "online" : "offline", pi->name);
		runqueue_task_cancel(&scr_other->proc.task, 1);
	}

	/* don't queue the same scripts twice */
	if (scr->proc.task.queued || scr->proc.task.running) {
		printlog(LOG_NOTICE, "'%s' scripts for '%s' already queued or running",
			 state_str, pi->name);
		return;
	}

	/* add runqueue task for running the scripts */
	printlog(LOG_NOTICE, "Scheduling '%s' scripts for '%s'", state_str, pi->name);
	scr->proc.task.type = &task_scripts_type;
	scr->proc.task.run_timeout = SCRIPTS_TIMEOUT * 1000;
	scr->intf = pi;
	scr->state = state_new;
	runqueue_task_add(&runq, &scr->proc.task, false);
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
