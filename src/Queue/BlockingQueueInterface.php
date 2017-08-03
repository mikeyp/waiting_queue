<?php

namespace Drupal\waiting_queue\Queue;

/**
 * An interface for DrupalQueue classes that can optionally block on claimItem.
 *
 * Code that uses queues that implement this interface can decide if it wants
 * the queue to block when claimItem is called. This means that the same queue
 * object can be useful when processing the queue in a user-facing page request,
 * or when processing by a backend daemon.
 */
interface BlockingQueueInterface {

  /**
   * Change the blocking behavior of the claimItem method.
   */
  public function setClaimItemBlocking($blocking);

}
