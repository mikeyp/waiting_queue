<?php

namespace Drupal\waiting_queue;

use Drupal\Core\Logger\LoggerChannelFactoryInterface;
use Drupal\Core\Queue\QueueFactory;
use Drupal\Core\Queue\QueueWorkerManagerInterface;
use Drupal\Core\Queue\RequeueException;
use Drupal\Core\Queue\SuspendQueueException;
use Drupal\Core\Site\Settings;
use Drupal\waiting_queue\Queue\BlockingQueueInterface;

class SignalQueueRunnerService {

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
   * The logger service.
   *
   * @var \Drupal\Core\Logger\LoggerChannelInterface
   */
  protected $logger;

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
  protected $signals = [];

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
   * QueueWorkerService constructor.
   */
  public function __construct(QueueFactory $queueFactory, QueueWorkerManagerInterface $queueManager, Settings $settings, LoggerChannelFactoryInterface $loggerChannelFactory) {
    $this->queueFactory = $queueFactory;
    $this->queueManager = $queueManager;
    $this->settings = $settings;
    $this->logger = $loggerChannelFactory->get('waiting_queue');
  }

  /**
   * Runs the named queue with no timeout and without any signal-based logic.
   *
   * @param string $queue_name
   *  Arbitrary string. The name of the queue to work with.
   */
  function processQueue($queue_name) {
    $default_reboot_signals = $this->settings->get('waiting_queue_reboot_signals', [SIGTERM, SIGINT]);
    $rebootSignals = $this->settings->get('waiting_queue_reboot_signals_' . $queue_name, $default_reboot_signals);
    $this->installHandlers($rebootSignals);

    // Make sure every queue exists. There is no harm in trying to recreate
    // an existing queue.
    $this->queueFactory->get($queue_name)->createQueue();

    // prepare a SIGALRM to be sent when the worker lifetime is spent.
    $lifetime = $this->settings->get('waiting_queue_process_lifetime', 5);
    pcntl_alarm($lifetime);
    if ($lifetime === 0) {
      $this->logger->warning('Queue "%queue" is being set up to run indefinitely. Probably not the best idea.', ['%queue' => $queue_name]);
    }

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
          $this->startJob($item);

          $queue_worker->processItem($item->data);
          $queue->deleteItem($item);

          $this->finishJob();
        } catch (RequeueException $e) {
          // The worker requested the task be immediately requeued.
          $queue->releaseItem($item);
          $this->finishJob();
        } catch (SuspendQueueException $e) {
          // If the worker indicates there is a problem with the whole queue,
          // release the item and skip to the next queue.
          $queue->releaseItem($item);
          $this->finishJob();

          watchdog_exception('waiting_queue', $e);
        } catch (\Exception $e) {
          // In case of any other kind of exception, log it and leave the item
          // in the queue to be processed again later.
          watchdog_exception('waiting_queue', $e);

          if (!empty($item) && $this->settings->get('waiting_queue_delete_on_exception', TRUE)) {
            $queue->deleteItem($item);
          }

          $this->finishJob();
        }
      }
    }
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

  /**
   * Informs the signal handler that job processing has finished.
   *
   * Calling this method does not imply success or failure of the job, only that
   * processing has ceased.
   */
  public function finishJob() {
    $this->processing = FALSE;
    // Reset interrupt state, even though it shouldn't matter.
    $this->interrupted = FALSE;

    if (TRUE === $this->rebootRequired) {
      $this->gracefulExit();
    }
  }

  /**
   * Informs the signal handler that a reboot is needed at the next possible
   * opportunity.
   */
  public function rebootRequired() {
    $this->rebootRequired = TRUE;
  }

  /**
   * Processes an incoming signal.
   *
   * This is the main signal handler method, used to process all signals except
   * SIGALRM.
   *
   * @param integer $signal
   *   The signal sent, as an integer.
   */
  public function signalHandler($signal, $siginfo) {
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
    else {
      $this->gracefulExit();
    }
  }

  /**
   * Processes an incoming SIGALRM, used for intentional timed self-termination.
   *
   * @param integer $signal
   *   The integer value of SIGALRM (14).
   */
  public function alarmHandler($signal, $siginfo) {
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
    // Simply exit. Child classes should put cleanup logic here, if needed.
    exit();
  }

  /**
   * Registers signal handlers with the current PHP process.
   *
   * @param array $rebootSignals
   */
  protected function installHandlers($rebootSignals) {
    if (in_array(SIGALRM, $rebootSignals)) {
      $str = 'Attempted to install a signal handler for SIGALRM, but waiting_queue already uses that signal internally. ';
      $this->logger->error($str);
      throw new \Exception($str, E_ERROR);
    }

    $this->signals = $rebootSignals;

    declare(ticks = 1);
    foreach ($rebootSignals as $signal) {
      $success = pcntl_signal($signal, [$this, 'signalHandler']);
    }
    pcntl_signal(SIGALRM, [$this, 'alarmHandler']);
  }

}
