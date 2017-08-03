<?php

namespace Drupal\waiting_queue\Queue;

use Drupal\Core\Database\Connection;

/**
 * Defines the key/value store factory for the blocking database backend.
 */
class DatabaseBlockingQueueFactory {

  /**
   * The database connection.
   *
   * @var \Drupal\Core\Database\Connection $connection
   */
  protected $connection;

  /**
   * Constructs this factory object.
   *
   * @param \Drupal\Core\Database\Connection $connection
   *   The Connection object containing the key-value tables.
   */
  public function __construct(Connection $connection) {
    $this->connection = $connection;
  }

  /**
   * Constructs a new queue object for a given name.
   *
   * @param string $name
   *   The name of the collection holding key and value pairs.
   *
   * @return \Drupal\waiting_queue\Queue\DatabaseBlockingQueue
   *   A key/value store implementation for the given $collection.
   */
  public function get($name) {
    return new DatabaseBlockingQueue($name, $this->connection);
  }

}
