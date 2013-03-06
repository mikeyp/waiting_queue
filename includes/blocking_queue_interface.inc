<?php

/**
 * @file
 *   Provides an interface for queues that can optionally block on claimItem.
 */

/**
 * An interface for DrupalQueue classes that can optionally block on claimItem.
 *
 * Code that uses queues that implement this interface can decide if it wants
 * the queue to block when claimItem is called. This means that the same queue
 * object can be useful when processing the queue in a user-facing page request,
 * or when processing by a backend daemon.
 */
interface WaitingQueueBlockingQueueInterface {

  /**
   * Change the blocking behavior of the claimItem method.
   */
  public function setClaimItemBlocking(boolean $blocking);

}
