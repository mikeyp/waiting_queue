<?php

/**
 * Implements hook_drush_help().
 */
function waiting_queue_drush_help($section) {
  switch ($section) {
    case 'drush:process-waiting-queue':
     return dt('Run the named, waiting Drupal queue. Allows for processing queues that block indefinitely waiting for new jobs without holding up other queues.');
  }
}

/**
 * Implements hook_drush_command().
 */
function waiting_queue_drush_command() {
  $items['process-waiting-queue'] = array(
    'callback' => 'waiting_queue_process_queue',
    'description' => 'Run the named, waiting Drupal queue.',
    'arguments' => array(
      'queue_to_process' => 'The name of the queue to run.',
    ),
  );
  return $items;
}

/**
 * Gateway to either a signal or non-signal based waiting_queue implementation.
 *
 * Signal handling gives us a much more robust and precise queue, so we use it
 * if it's at all available.
 *
 * @param string $queue_name
 */
function waiting_queue_process_queue($queue_name) {
  set_time_limit(0);
  if (function_exists('pcntl_signal')) {
    require_once dirname(__FILE__) . '/waiting_queue.signals.inc';
    _waiting_queue_process_queue_signals($queue_name);
  }
  else {
    require_once dirname(__FILE__) . '/waiting_queue.nosignals.inc';
    _waiting_queue_process_queue_nosignals($queue_name);
  }
}
