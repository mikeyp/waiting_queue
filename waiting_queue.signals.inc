<?php

class WaitingQueueSignalHandler {

  /**
   * Flag indicating that a restart is necessary.
   *
   * @var bool
   */
  protected $rebootRequired = FALSE;

  /**
   * Flag indicating that a job is currently being processed.
   *
   * @var bool
   */
  protected $processing = FALSE;

  /**
   * An array of all the signals for which handlers have been registered.
   *
   * @var array
   */
  protected $signals = array();

  /**
   * State variable that tracks if an interrupt has already been sent once on
   * this job cycle.
   *
   * By keeping track of this, we can allow external actors to forcibly shut
   * interrupt a job while in process without having to resort to SIGKILL.
   *
   * We do not reuse $rebootRequired in order to clearly separate the expected
   * SIGALRM case from an unexpected external signal.
   *
   * @var bool
   */
  protected $interrupted = FALSE;

  /**
   * Create a new signal handler helper, attaching to it the set of desired
   * signals.
   *
   * @param array $rebootSignals
   *   An array of signals (e.g., SIGINT, SIGTERM, SIGHUP). The only disallowed
   *   signal is SIGALRM, as that is already in use internally.
   */
  public function __construct(array $rebootSignals = array()) {
    if (in_array(SIGALRM, $rebootSignals)) {
      $str = 'Attempted to install a signal handler for SIGALRM, but waiting_queue already uses that signal internally. ';
      watchdog('waiting_queue', $str, array(), WATCHDOG_ERROR);
      throw new Exception($str, E_ERROR);
    }
    $this->installHandlers($rebootSignals);
  }

  /**
   * Informs the signal handler that job processing is commencing.
   *
   * @param mixed $item
   *   The job payload that is about to be processed. There are no restrictions
   *   on payload format, though they tend to be serialize()d strings.
   */
  public function startJob($item) {
    $this->processing = TRUE;
  }

  public function finishJob() {
    $this->processing = FALSE;
    // Reset interrupt state, even though it shouldn't matter.
    $this->interrupted = FALSE;

    if (TRUE === $this->rebootRequired) {
      $this->gracefulExit();
    }
  }

  public function rebootRequired() {
    $this->rebootRequired = TRUE;
  }

  public function signalHandler($signal) {
    if (TRUE === $this->processing) {
      $this->rebootRequired();

      if (SIGINT === $signal) {
        if (FALSE === $this->interrupted) {
          drush_print('A job is currently being processed; the worker will be shut down when the job is finished. Press Ctrl-C again to quit immediately without waiting for the job to complete.');
          $this->interrupted = TRUE;
        }
        else {
          drush_print('Shutting down immediately...');
          $this->gracefulExit();
        }
      }
    }
  }

  public function alarmHandler($signal) {
    if (TRUE === $this->processing) {
      $this->rebootRequired();
    }
    else {
      $this->gracefulExit();
    }
  }

  /**
   * Terminate the current process as safely as possible.
   */
  protected function gracefulExit() {
    // Use POSIX self-exiting to be slightly safer and more API-friendly...but
    // only if there isn't a custom signal handler installed for SIGTERM.
    if (function_exists('posix_kill') && !in_array(SIGTERM, $this->signals)) {
      posix_kill(posix_getpid(), SIGTERM);
      // Probably will never get here, but ensure the signal goes. See example
      // in http://www.php.net/manual/en/function.pcntl-signal-dispatch.php
      pcntl_signal_dispatch();
    }
    // Otherwise, exit the old-fashioned way.
    else {
      // TODO need to do a little investigating to make sure that exiting like
      // this is a safe enough option.
      exit();
    }
  }

  protected function installHandlers($rebootSignals) {
    $this->signals = $rebootSignals;

    declare(ticks = 1);
    foreach ($rebootSignals as $signal) {
      pcntl_signal($signal, array($this, 'signalHandler'));
    }
    pcntl_signal(SIGALRM, array($this, 'alarmHandler'));
  }
}

/**
 * Runs the named queue using signal handling for flow control.
 *
 * @param string $queue_name
 *  Arbitrary string. The name of the queue to work with.
 */
function _waiting_queue_process_queue_signals($queue_name) {
  $queue = DrupalQueue::get($queue_name);
  $function = waiting_queue_get_callback_function($queue_name);

  $default_reboot_signals = variable_get('waiting_queue_reboot_signals', array(SIGTERM, SIGINT));
  $reboot_signals = variable_get('waiting_queue_reboot_signals_' . $queue_name, $default_reboot_signals);
  $signal_handler = new WaitingQueueSignalHandler($reboot_signals);
  if (variable_get('waiting_queue_print_pid_' . $queue_name, FALSE) || variable_get('waiting_queue_print_pid', FALSE)) {
    drush_print("Waiting queue working starting with pid: " . getmypid());
  }

  // prepare a SIGALRM to be sent when the worker lifetime is spent.
  $lifetime = variable_get('waiting_queue_process_lifetime', 3600);
  pcntl_alarm($lifetime);
  if ($lifetime === 0) {
    watchdog('waiting_queue', 'Queue "%queue" is being set up to run indefinitely. Probably not the best idea.', array('%queue' => $queue_name), WATCHDOG_WARNING);
  }

  while (TRUE) {
    try {
      while ($item = $queue->claimItem()) {
        $signal_handler->startJob($item);

        $function($item->data);
        $queue->deleteItem($item);

        $signal_handler->finishJob();
      }
    }
    catch (Exception $e) {
      $error_info = array(
        '%queue_name' => $queue_name,
        '%error_message' => $e->getMessage(),
      );
      watchdog('waiting_queue', t("Error processing queue %queue_name, got error '%error_message'.", $error_info));
      if (!empty($item) && waiting_queue_delete_on_exception($queue_name)) {
        $queue->deleteItem($item);
      }
      $signal_handler->finishJob();
    }
  }
}
