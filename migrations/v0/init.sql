/**
 * Run a migration on this sql file with the default postgres user
 * ❯ sudo -u postgres psql -f init.sql
 */

DO $$ 
BEGIN
  IF NOT EXISTS (SELECT FROM pg_user WHERE usename = 'renaissance_app') THEN
    CREATE USER renaissance_app;
  END IF;
END $$;

 -- `CREATE DATABASE` cannot be executed inside a transaction block, so we use `\gexec` instead.
SELECT 'CREATE DATABASE renaissance OWNER renaissance_app' WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname = 'renaissance')\gexec

\connect renaissance;
CREATE SCHEMA IF NOT EXISTS app AUTHORIZATION renaissance_app;

-- Let's make sure no one uses the public schema.
REVOKE ALL PRIVILEGES ON SCHEMA public FROM PUBLIC;

-- to use uuid_generate_v4() function
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

/**
 * After creating the renaissance_app user, need to:
 * 
 * 1. ❯ sudo -u postgres psql
 * 2. postgres=# ALTER USER renaissance_app WITH PASSWORD 'password';
 * 3. postgres=# \q
 * 4. ❯ sudo service postgresql restart
 * 5. ❯ psql -U renaissance_app -h localhost -d renaissance -W
 * 6. renaissance=> ...
 * 
 * We don't parameterize the andlifecate_app user password in a script
 * because command-line arguments can be visible in process lists or logs
 */