# Database Load

Allows you to select a database dump from a remote server for download and
database import.

## How it Works

 * SCP is used to generate a list of database options by analyzing a configured
   directory on a remote server.
 * rsync is used to download the selected database.
 * The user is offered options to drop the current database tables and replace
   with this dump.
 * The user is offered the option to have updatedb automatically run after the
   database import.
 * The manifest and downloaded database is cached by calendar date by default.

## Configuration
 * It is advisable that any repository making use of this provide most of the
   configuration values. This can be done with the trick of maintaining a
   drushrc.php file inside a Git repository. (At GoingOn, we already advise this,
   and are working to automate it for new development environments.)
 * In your own drushrc.php (such as you might place in your <HOMEDIR>/.drush
   directory) you should configure your user account if different from the
   current system.

~~~
    $command_specific['database-load']['remote-user'] = 'my-remote-account';
~~~

## Customization

Implement drush_hook_post_database_load() to make tweaks to the database after 
loading, such as resetting the user/1 password.
