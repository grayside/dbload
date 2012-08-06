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
      'destination' => 'The local path where database dumps are stashed.',
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
  $args['destination'] = drush_get_option('destination', drush_directory_cache('dbload'));
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
  drush_log(dt('Database "!db" from !date ready at !path.', array(
    '!db' => $db,
    '!date' => dbload_db_mtime($db_path),
    '!path' => $db_path,
  )), 'status');

  if (drush_confirm('Download site files?') && drush_get_option('remote-files', FALSE)
      && drush_bootstrap(DRUSH_BOOTSTRAP_DRUPAL_FULL)) {
    dbload_retrieve_site_files($db, $args);    
  }
  if (drush_confirm(dt('Load the latest dump for !db into the database?', array('!db' => $db)))) {
    drush_log('Loading data into database.', 'status');
    drush_invoke_process('@self', 'sql-drop', array(), array('y'));
    drush_shell_exec("drush sql-cli 2> /dev/null < {$db_path}");
  }
  if (drush_confirm('Run updatedb?')) {
    drush_invoke_process('@self', 'updatedb', array(), array('y'));
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
  $filename = "{$db}.sql";

  $filepath = $args['destination'] . '/' . $filename;

  if (drush_get_option('force-refresh', FALSE) || !file_exists($filepath) || !_dbload_is_cached($filepath)) {
    drush_mkdir($args['destination']);
    drush_shell_exec("rsync -acz {$args['remote-user']}@{$args['server']}:{$args['remote-path']}/{$db}-safe.sql {$filepath} 2> /dev/null");
    drush_log(dt('Database from !datestamp downloaded for !customer.', array(
      '!datestamp' => dbload_db_mtime($filepath),
      '!db' => $db,
    )));
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
 * Replace the existing files directory with one from the remote server.
 *
 * @param $db
 *  The specific database variant to target for database retrieval.
 * @param $args
 *  The sanitized args to use to download the database, if needed.
 *
 * @return String
 *   Path to the local destination.
 */
function dbload_retrieve_site_files($db, $args) {
  $remote_files = drush_get_option('remote-files', FALSE) . '/*';
  // Simple token replacement in case the remote files path is "creative".
  $remote_files = str_replace('{db}', $db, $remote_files);

  if (drush_drupal_major_version() < 7) {
    $filepath = drush_get_context('DRUSH_SELECTED_DRUPAL_ROOT') . '/' . file_directory_path();
  }
  else {
    $filepath = drupal_realpath(file_default_scheme() . '://');
  }

  drush_delete_dir($file_path . '/*');
  drush_shell_exec("rsynch -aczR {$args['remote-user']}@{$args['server']}:{$remote_files} {$filepath} 2> /dev/null");

  return $filepath;
}

/**
 * Retrieve an array with all clients.
 *
 * @param $args
 *  Arguments for accessing the server for database listing retrieval.
 * @param $reset
 *  (Optional) Reset the static cache. This implies a reload from the local
 *  filesystem, with fallback to downloading new data. Local data is invalidated
 *  each day.
 *
 * @todo
 *  Create a more automatic approach for dealing with client listings.
 */
function _dbload_db_list($args, $reset = FALSE) {
  static $dbs;

  if (!empty($dbs) && !$reset) {
    return $dbs;
  }

  $remote_user = $args['remote-user'] === NULL ? '' : "{$args['remote-user']}@";
  $cmd = "ssh {$remote_user}{$args['server']} 'ls *sql {$args['remote-path']}' 2> /dev/null";

  // Create a unique manifest file for each set of server connection information.
  $id = md5($cmd);
  $index = $args['destination'] . "/dbload-index-{$id}.dat";

  if (drush_get_option('force-refresh', FALSE) || !file_exists($index) || !_dbload_is_cached($index)) {
    drush_mkdir($args['destination']);
    drush_shell_exec($cmd);
    $dbs = drush_shell_exec_output();

    $output = array();
    foreach ($dbs as $db) {
      if (preg_match('/(.*)\-safe\.sql$/S', $db, $match)) {
        $output[$match[1]] = $match[1];
      }
    }
    $dbs = drush_map_assoc(array_filter($output));
    // Only write the new manifest file if dbload finds something on the remote
    // server.
    if (count($dbs)) {
      _dbload_write_manifest($index, $dbs, array('connection' => $args));
    }
  }
  else {
    $dbs = _dbload_read_manifest($index);
  }

  return $dbs;
}

/**
 * Write a dbload manifest file.
 *
 * @param $filename
 *   Path to the manifest file on disk.
 * @param $dbs
 *   Array of databases identified for the manifest.
 * @param $additional
 *   Array with additional data to be included in the manifest file.
 *
 * @return boolean
 */
function _dbload_write_manifest($filename, $dbs, $additional) {
  $dbs = drush_map_assoc($dbs);
  
  // Metadata is included for development and advanced usage troubleshooting.
  $data = array(
    'title' => 'Database Manifest',
    'date' => date('r'),
    'generated by' => 'dbload v1.2',
    'dbs' => $dbs,
  ) + $additional;

  return file_put_contents($filename, drush_json_encode($data));
}

/**
 * Read a dbload manifest file.
 *
 * @param $filename
 *   Path to the manifest file on disk.
 *
 * @return
 *   Array of database names keyed on their values.
 */
function _dbload_read_manifest($filename) {
  $data = @file_get_contents($filename);
  if (empty($data)) {
    return array();
  }
  $data = drush_json_decode($data);
  return $data['dbs'];
}

/**
 * Check if a given file can be used as a valid data cache.
 *
 * dbload presumes the remote collection of database dumps is routinely
 * refreshed on a daily basis. If the local data is from a previous day, dbload
 * should try to retrieve new data. If there is no new data to be had (such as
 * because of network connectivity issues) dbload should retain the existing
 * cache.
 *
 * The --force-refresh option can be used to ignore cached data.
 *
 * @param $filename
 *   Path to a file on disk.
 *
 * @return boolean
 */
function _dbload_is_cached($filename) {
  $mtime = filemtime($filename);
  $today = strtotime('today');

  drush_log(dt('Checking the status of `!file`.', array('!file' => $index)), 'debug');
  drush_log(dt("File Modified Time: !mtime; Today's Timestamp: !datestamp", array(
    '!mtime' => $mtime,
    '!datestamp' => $today,
  )), 'debug');

  return $mtime > $today;
}

/**
 * Get a nicely formatted timestamp for a file modified date.
 *
 * @param string $filepath
 *  Local path to the file.
 *
 * @return string
 */
function dbload_db_mtime($filepath) {
  return date ("F d Y H:i:s", filemtime($filepath));
}

