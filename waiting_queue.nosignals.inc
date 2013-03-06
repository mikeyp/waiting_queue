<?php

/**
 * Runs the named queue with no timeout and without any signal-based logic.
 *
 * @param string $queue_name
 *  Arbitrary string. The name of the queue to work with.
 */
function _waiting_queue_process_queue_nosignals($queue_name) {
  $default_queue_process_lifetime = variable_get('waiting_queue_process_lifetime', 3600);
  $end_time = variable_get('waiting_queue_process_lifetime_' . $queue_name, $default_queue_process_lifetime) + time();

  $queue = DrupalQueue::get($queue_name);
  // If the queue implements our interface for optionally blocking on claimItem,
  // we set it to block.
  if ($queue instanceof WaitingQueueBlockingQueueInterface) {
    $queue->setClaimItemBlocking(TRUE);
  }
  $function = waiting_queue_get_callback_function($queue_name);

  if (variable_get('waiting_queue_print_pid_' . $queue_name, FALSE) || variable_get('waiting_queue_print_pid', FALSE)) {
    drush_print("Waiting queue working starting with pid: " . getmypid());
  }

  while (TRUE) {
    try {
      while ($item = $queue->claimItem()) {
        // The $queue->claimItem() call may block for a long time, so we check
        // two things before processing a job: a) that we haven't exceeded the
        // process lifetime, and b) whether a reboot is required.
        if (time() > $end_time) {
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
    if (time() > $end_time) {
      exit();
    }
  }
}
