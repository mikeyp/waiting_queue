Waiting Queue
=============

Simple Drupal module intended to make it easy to run a queue with workers that
wait indefinitely for jobs.

Such worker processes should be managed by something like Supervisord. Writing
daemons should always be avoided, mkay?

Using with Drupal's standard queues
-----------------------------------

Drupal's default `DatabaseQueue` class is non-blocking, which means that if the
queue is empty, and you point a Waiting queue processor at it, it's going to
poll your database for new items as often as it can, e.g. it's going to put
significant load on it. As a workaround, if using an external blocking queue
system like Beanstalkd is not possible, Waiting Queue provides a simple
extension to the `DatabaseQueue` that polls the database at a configurable interval,
defaulting to 1 second.

To use this, simply change the queue class in your settings.php:

    $settings['queue_service_QUEUE_NAME'] = 'waiting_queue.database_blocking_queue';

or

    $settings['queue_default'] = 'waiting_queue.database_blocking_queue';

As far as other systems as concerned the queue will be the same, but Waiting
Queue processors will set the queue to block when claiming items for better
performance.
