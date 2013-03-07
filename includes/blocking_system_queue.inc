<?php

/**
 * @file
 *   Provides a class that allows a SystemQueue to block on claimItem.
 */

/**
 * A blocking version of the default SystemQueue.
 *
 * This class simply makes calls to its claimItem method optionally block, this
 * allows you to run queue processors that can block until there's an item to process.
 */
class WaitingQueueBlockingSystemQueue extends SystemQueue implements WaitingQueueBlockingQueueInterface {

  /**
   * Boolean indicating if this queue's claimItem method is going to block.
   */
  protected $blocking = FALSE;

  /**
   * The the amount of time in microseconds to wait between polling the queue.
   */
  public $pollingInterval = 1000000;

  /**
   * Change the blocking behavior of the claimItem method.
   *
   * By default the claimItem method in this class will not block until there
   * is an item to return, but optionally you can use this method to change
   * this behavior so that it will block.
   *
   * @param $blocking
   *   The new blocking status to set.
   */
  public function setClaimItemBlocking(boolean $blocking) {
    $this->blocking = $blocking;
  }

  public function claimItem($lease_time = 30) {
    // If we're not blocking, just pass through to the original implementation.
    if (!$this->blocking) {
      return parent::claimItem($lease_time);
    }
    // Otherwise, start a polling loop.
    do {
      // Try to get an item from the queue.
      $item = parent::claimItem($lease_time);
      // If we didn't get an item, then usleep for the polling interval, and
      // repeat the loop. We need the || TRUE at the end, because the usleep
      // function returns NULL which would exit our loop.
    } while (($item === FALSE) && (usleep($this->pollingInterval) || TRUE));
    return $item;
  }
}
