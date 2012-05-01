<?php
/**
 * @file
 *  Code for the Database Load command.
 */

/**
 * Implementation of hook_drush_command().
 */
function dbload_drush_command() {
  $items = array();

  $items['database-load'] = array(
    'title' => 'Database Load',
    'description' => 'Load a database from some location into the currently active Drupal site.',
    'arguments' => array(
      'client' => 'Grab a database related to the specified client.',
    ),
    'options' => array(
      'server' => 'Server holding the data.',
      'remote-user' => 'Optional, your username for the server or service hosting the database.',
      'remote-path' => 'Path to the database file.',
      'destination' => 'The local path where database dumps are stashed.',
      'no-refresh-limit' => 'The database will normally be re-downloaded only once per day. Use this flag to force download.',
    ),
    'bootstrap' => 'DRUSH_BOOTSTRAP_DRUSH',
    'aliases' => array('dbload', 'db-load', 'dbl'),
  );

  return $items;
}

/**
 * Implementation of hook_drush_help()
 */
function dbload_drush_help($section) {
  switch ($section) {
    case 'drush:database-load':
      return file_get_contents(dirname(__FILE__) . '/README.txt');
    case 'error:DBLOAD_NO_DB_OPTIONS':
      return dt('No database options could be found. Remote host may be unavailable.');
  }
}

/**
 * Implementation of drush_hook_COMMAND() for database-load.
 */
function drush_dbload_database_load($db = NULL) {
  $args['server'] = drush_get_option('server', NULL);
  $args['remote-path'] = drush_get_option('remote-path', NULL);
  $args['destination'] = drush_get_option('destination', '/tmp');

  $error = FALSE;
  foreach ($args as $name => $arg) {
    if (is_null($arg)) {
      drush_set_error('DRUSH_MISSING_OPTION', dt('Missing the !arg argument.', array('!arg' => $name)));
      $error = TRUE;
    }
  }
  if ($error) {
    return FALSE;
  }
  $args['remote-user'] = drush_get_option('remote-user', NULL);

  if (is_null($db) || !array_key_exists($db, _dbload_db_list($args))) {
    $options = _dbload_db_list($args);
    if (count($options) == 0) {
      return drush_set_error('DBLOAD_NO_DB_OPTIONS');
    }
    $db = drush_choice(_dbload_db_list($args));
  }

  // Cancelled state reached.
  if (!$db) {
    return FALSE;
  }
 
  drush_print('Initiating database retrieval. Please hold.');
  $db_path = dbload_retrieve_database($db, $args);

  if (!$db_path) {
    return FALSE;
  }

  if (drush_confirm(dt('Load the latest dump for !db into the database?', array('!db' => $db)))) {
    drush_shell_exec("drush -yd sql-drop");
    drush_shell_exec("drush sql-cli 2> /dev/null < {$db_path}");
  }
  else {
    return FALSE;
  }
}

/**
 * Retrieve the database dump to load into the live site.
 *
 * Only downloads a fresh copy if the current database is older than the refresh limit.
 *
 * @param $db
 *  The specific database variant to target for database retrieval.
 * @param $args
 *  The sanitized args to use to download the database, if needed.
 *
 * @return String Path to the local destination.
 */
function dbload_retrieve_database($db, $args) {
  $date = date('Y-m-d');
  $filename = "prod-{$db}-{$date}.sql";
  $filepath = $args['destination'] . '/' . $filename;

  if (drush_get_option('no-refresh-limit', FALSE) || !file_exists($filepath)) {
    drush_mkdir($args['destination']);
    drush_shell_exec("scp {$args['remote-user']}@{$args['server']}:{$args['remote-path']}/{$db}-safe.sql {$filepath} 2> /dev/null");
  }
  else {
    drush_print("Database cached on disk. To retrieve a fresh copy rerun with '--no-refresh-limit'.");
  }
  
  if (!file_exists($filepath)) {
    return drush_set_error('DRUSH_DB_RETRIEVAL_FAIL', dt('Could not find the !db database.', array('!db' => $db)));
  }
  return $filepath;
}

/**
 * Retrieve an array with all clients.
 *
 * @param $args
 *  Arguments for accessing the server for database listing retrieval.
 * @param $reset
 *  (Optional) Reset the static cache.
 *
 * @todo
 *  Create a more automatic approach for dealing with client listings.
 */
function _dbload_db_list($args, $reset = FALSE) {
  static $dbs;

  if (!empty($dbs) || $reset) {
    return $dbs;
  }

  $date = date('Y-m-d');
  $index = $args['destination'] . "/prod-index-{$date}.dat";
 
  if (drush_get_option('no-refresh-limit', FALSE) || !file_exists($index)) {
    drush_mkdir($args['destination']);

    $remote_user = $args['remote-user'] === NULL ? '' : "{$args['remote-user']}@";

    drush_shell_exec("ssh {$remote_user}{$args['server']} 'ls *sql {$args['remote-path']}' 2> /dev/null");

    $dbs = drush_shell_exec_output();
    $output = '';
    foreach ($dbs as $db) {
      if (preg_match('/(.*)\-safe\.sql$/S', $db, $match)) {
        $output[$match[1]] = $match[1];
      }
    }
    $dbs = $output;
    if (count($dbs)) {
      $output = implode("\n", $output);
      file_put_contents($index, $output);
    }
    return $dbs;
  }

  $items = explode("\n", file_get_contents($index));
  $dbs = array();
  foreach ($items as $db) {
    $dbs[$db] = $db;
  }

  return $dbs;
}

