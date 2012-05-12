<?php

class WaitingQueueSignalHandler {

  protected $rebootRequired = FALSE;

  public function __construct(array $rebootSignals = array()) {
    $this->installHandlers($rebootSignals);
  }

  public function rebootRequired() {
    return $this->rebootRequired;
  }

  public function signalHandler($signal) {
    $this->rebootRequired = TRUE;
  }

  public function installHandlers($rebootSignals) {
    declare(ticks = 1);
    foreach ($rebootSignals as $signal) {
      pcntl_signal($signal, array($this, 'signalHandler'));
    }
  }
}

/**
 * Runs the named queue using signal handling for flow control.
 *
 * @param string $queue_name
 *  Arbitrary string. The name of the queue to work with.
 */
function _waiting_queue_process_queue_signals($queue_name) {
  $default_queue_process_lifetime = variable_get('waiting_queue_process_lifetime', 3600);
  $end_time = variable_get('waiting_queue_process_lifetime_' . $queue_name, $default_queue_process_lifetime) + time();

  $queue = DrupalQueue::get($queue_name);
  $function = waiting_queue_get_callback_function($queue_name);

  $default_reboot_signals = variable_get('waiting_queue_reboot_signals', array(SIGTERM));
  $reboot_signals = variable_get('waiting_queue_reboot_signals_' . $queue_name, $default_reboot_signals);
  $signal_handler = new WaitingQueueSignalHandler($reboot_signals);
  if (variable_get('waiting_queue_print_pid_' . $queue_name, FALSE) || variable_get('waiting_queue_print_pid', FALSE)) {
    drush_print("Waiting queue working starting with pid: " . getmypid());
  }

  while (TRUE) {
    try {
      while ($item = $queue->claimItem()) {
        // The $queue->claimItem() call may block for a long time, so we check
        // two things before processing a job: a) that we haven't exceeded the
        // process lifetime, and b) whether a reboot is required.
        if (time() > $end_time || $signal_handler->rebootRequired()) {
          $queue->releaseItem($item);
          exit();
        }

        $function($item->data);
        $queue->deleteItem($item);
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
    }

    // If we caught an error, or $queue->claimItem() didn't return a job, we
    // can end up here, and it could be a long time since we last checked
    // process lifetime or reboot flag, so check again.
    if (time() > $end_time || $signal_handler->rebootRequired()) {
      exit();
    }
  }
}
