<?php
/**
 * @file
 *  Code for the Database Load command.
 */

/**
 * Implements hook_drush_command().
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
      'destination' => 'The local path where database dumps are stashed. Defaults to /tmp.',
      'force-refresh' => 'The database will normally be re-downloaded only once per day. Use this flag to force download.',
      'connection' => 'Connection settings for database retrieval.',
    ),
    'sub-options' => array(
      'connection' => array(
        'server' => 'Server holding the data.',
        'remote-user' => 'Optional, your username for the server or service hosting the database.',
        'remote-path' => 'Path to the database file.',
      ),
    ),
    'bootstrap' => 'DRUSH_BOOTSTRAP_DRUSH',
    'aliases' => array('dbload', 'dbl'),
  );

  return $items;
}

/**
 * Implements hook_drush_help()
 */
function dbload_drush_help($section) {
  switch ($section) {
    case 'drush:database-load':
      return file_get_contents(dirname(__FILE__) . '/README.md');
    case 'error:DBLOAD_NO_SERVER':
      return dt('Failed to specify a valid host.');
    case 'error:DBLOAD_NO_REMOTE_PATH':
      return dt('Failed to specify a valid remote path.');
    case 'error:DBLOAD_NO_DB_OPTIONS':
      return dt('Could not find any available databases. Are you connected to the network?');
  }
}

/**
 * Implements drush_hook_COMMAND_validate() for database-load.
 */
function drush_dbload_database_load_validate($db = NULL) {
  if (!drush_get_option('server', FALSE)) {
    drush_set_error('DBLOAD_NO_SERVER');
  }
  if (!drush_get_option('remote-path', FALSE)) {
    drush_set_error('DBLOAD_NO_REMOTE_PATH');
  }
}

/**
 * Implements drush_hook_COMMAND() for database-load.
 */
function drush_dbload_database_load($db = NULL) {
  $args['server'] = drush_get_option('server', NULL);
  $args['remote-path'] = drush_get_option('remote-path', NULL);
  $args['destination'] = drush_get_option('destination', '/tmp');
  $args['remote-user'] = drush_get_option('remote-user', NULL);

  if ($db == NULL || !array_key_exists($db, _dbload_db_list($args))) {
    $options = _dbload_db_list($args);
    if (empty($options)) {
      return drush_set_error('DBLOAD_NO_DB_OPTIONS');
    }
    $db = drush_choice(_dbload_db_list($args));
  }
  // Valid DB not found or user cancelled operation.
  if (!$db) {
    return FALSE;
  }
 
  drush_log('Retrieving the selected database...', 'status');
  $db_path = dbload_retrieve_database($db, $args);
  if (!$db_path) {
    return FALSE;
  }
  drush_log('Database downloaded.', 'success');
  drush_log(dt('Database !db downloaded to !path', array(
    '!db' => $db,
    '!path' => $db_path,
  )));
     
  if (drush_has_boostrapped(DRUSH_BOOTSTRAP_DRUPAL_DATABASE) || drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_DATABASE)) {
    if (drush_confirm(dt('Load the latest dump for !db into the database?', array('!db' => $db)))) {
      drush_log('Loading data into database.', 'status');
      drush_invoke_process('@self', 'sql-drop', array(), array('y'));
      drush_shell_exec("drush sql-cli 2> /dev/null < {$db_path}");
    }
    
    if (drush_confirm('Run updatedb?')) {
      drush_invoke_process('@self', 'updatedb', array(), array('y'));
    }
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

  if (drush_get_option('force-refresh', FALSE) || !file_exists($filepath)) {
    drush_mkdir($args['destination']);
    drush_shell_exec("rsync -acz {$args['remote-user']}@{$args['server']}:{$args['remote-path']}/{$db}-safe.sql {$filepath} 2> /dev/null");
  }
  else {
    drush_print("Database cached on disk. To retrieve a fresh copy rerun with '--force-refresh'.");
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

  if (!empty($dbs) && !$reset) {
    return $dbs;
  }

  $date = date('Y-m-d');
  $index = $args['destination'] . "/prod-index-{$date}.dat";

  if (drush_get_option('force-refresh', FALSE) || !file_exists($index)) {
    drush_mkdir($args['destination']);

    $remote_user = $args['remote-user'] === NULL ? '' : "{$args['remote-user']}@";
    $cmd = "ssh {$remote_user}{$args['server']} 'ls *sql {$args['remote-path']}' 2> /dev/null";
    drush_shell_exec($cmd);
    $dbs = drush_shell_exec_output();

    $output = array();
    foreach ($dbs as $db) {
      if (preg_match('/(.*)\-safe\.sql$/S', $db, $match)) {
        $output[$match[1]] = $match[1];
      }
    }
    $dbs = $output;
    if (count($dbs)) {
      $output = array_filter($output);
      $output = implode("\n", $output);
      file_put_contents($index, $output);
    }
    return $dbs;
  }

  $items = explode("\n", @file_get_contents($index));
  $dbs = array();
  foreach ($items as $db) {
    $dbs[$db] = $db;
  }

  return $dbs;
}

