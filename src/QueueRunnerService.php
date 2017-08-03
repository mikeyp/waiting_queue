<?php

namespace Drupal\waiting_queue;

use Drupal\Core\Queue\QueueFactory;
use Drupal\Core\Queue\QueueWorkerManagerInterface;
use Drupal\Core\Queue\RequeueException;
use Drupal\Core\Queue\SuspendQueueException;
use Drupal\Core\Site\Settings;
use Drupal\waiting_queue\Queue\BlockingQueueInterface;

class QueueRunnerService {

  /**
   * The queue factory service.
   *
   * @var \Drupal\Core\Queue\QueueFactory
   */
  protected $queueFactory;

  /**
   * The queue manager service.
   *
   * @var \Drupal\Core\Queue\QueueWorkerManagerInterface
   */
  protected $queueManager;

  /**
   * The settings service.
   *
   * @var \Drupal\Core\Site\Settings
   */
  protected $settings;

  /**
   * QueueWorkerService constructor.
   */
  public function __construct(QueueFactory $queueFactory, QueueWorkerManagerInterface $queueManager, Settings $settings) {
    $this->queueFactory = $queueFactory;
    $this->queueManager = $queueManager;
    $this->settings = $settings;
  }


  /**
   * Runs the named queue with no timeout and without any signal-based logic.
   *
   * @param string $queue_name
   *  Arbitrary string. The name of the queue to work with.
   */
  function processQueue($queue_name) {
    // Make sure every queue exists. There is no harm in trying to recreate
    // an existing queue.
    $this->queueFactory->get($queue_name)->createQueue();

    $default_queue_process_lifetime = $this->settings->get('waiting_queue_process_lifetime', 3600);
    $end_time = $this->settings->get('waiting_queue_process_lifetime_' . $queue_name, $default_queue_process_lifetime) + time();

    $queue = $this->queueFactory->get($queue_name);

    // If the queue implements our interface for optionally blocking on
    // claimItem, we set it to block.
    if ($queue instanceof BlockingQueueInterface) {
      $queue->setClaimItemBlocking(TRUE);
    }

    $queue_worker = $this->queueManager->createInstance($queue_name);

    if ($this->settings->get('waiting_queue_print_pid_' . $queue_name, FALSE) || $this->settings->get('waiting_queue_print_pid', FALSE)) {
      drush_print("Waiting queue working starting with pid: " . getmypid());
    }

    while (TRUE) {
      while ($item = $queue->claimItem()) {
        try {
          // The $queue->claimItem() call may block for a long time, so we check
          // two things before processing a job: a) that we haven't exceeded the
          // process lifetime, and b) whether a reboot is required.
          if (time() > $end_time) {
            $queue->releaseItem($item);
            exit();
          }

          $queue_worker->processItem($item->data);
          $queue->deleteItem($item);
        } catch (RequeueException $e) {
          // The worker requested the task be immediately requeued.
          $queue->releaseItem($item);
        } catch (SuspendQueueException $e) {
          // @TODO Figure out how to handle this, as it only really makes sense with cron...maybe set a timeout and try again later with some sort of backoff??
          // If the worker indicates there is a problem with the whole queue,
          // release the item and skip to the next queue.
          $queue->releaseItem($item);

          watchdog_exception('waiting_queue', $e);
        } catch (\Exception $e) {
          // In case of any other kind of exception, log it and leave the item
          // in the queue to be processed again later.
          watchdog_exception('waiting_queue', $e);
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

}
