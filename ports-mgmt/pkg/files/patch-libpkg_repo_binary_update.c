--- libpkg/repo/binary/update.c.orig	2023-06-29 13:25:53 UTC
+++ libpkg/repo/binary/update.c
@@ -26,6 +26,7 @@
 
 #include <sys/stat.h>
 #include <sys/param.h>
+#include <sys/file.h>
 #include <sys/time.h>
 
 #include <stdio.h>
@@ -563,12 +564,12 @@ cleanup:
 		if (pkgdb_transaction_commit_sqlite(sqlite, "REPO") != EPKG_OK)
 			rc = EPKG_FATAL;
 	}
-	/* restore the previous db in case of failures */
-	if (rc != EPKG_OK && rc != EPKG_UPTODATE) {
-		unlink(name);
-		rename(path, name);
-	}
 	if (path != NULL) {
+		/* restore the previous db in case of failures */
+		if (rc != EPKG_OK && rc != EPKG_UPTODATE) {
+			unlink(name);
+			rename(path, name);
+		}
 		unlink(path);
 		free(path);
 	}
@@ -586,13 +587,14 @@ int
 pkg_repo_binary_update(struct pkg_repo *repo, bool force)
 {
 	char filepath[MAXPATHLEN];
+	char *lockpath = NULL;
 	const char update_finish_sql[] = ""
 		"DROP TABLE repo_update;";
 	sqlite3 *sqlite;
 
 	struct stat st;
 	time_t t = 0;
-	int res = EPKG_FATAL;
+	int ld, res = EPKG_FATAL;
 
 	bool got_meta = false;
 
@@ -603,7 +605,7 @@ pkg_repo_binary_update(struct pkg_repo *repo, bool for
 
 	pkg_debug(1, "PkgRepo: verifying update for %s", pkg_repo_name(repo));
 
-	/* First of all, try to open and init repo and check whether it is fine */
+	/* First of all, try to open repo and check whether it is fine */
 	if (repo->ops->open(repo, R_OK|W_OK) != EPKG_OK) {
 		pkg_debug(1, "PkgRepo: need forced update of %s", pkg_repo_name(repo));
 		t = 0;
@@ -627,6 +629,15 @@ pkg_repo_binary_update(struct pkg_repo *repo, bool for
 		}
 	}
 
+	xasprintf(&lockpath, "%s-lock", filepath);
+	ld = open(lockpath, O_CREAT|O_TRUNC|O_WRONLY, 00644);
+	if (flock(ld, LOCK_EX|LOCK_NB) == -1) {
+		pkg_emit_notice("Another process is updating repository %s",
+			repo->name);
+		res = EPKG_FATAL;
+		goto cleanup;
+	}
+
 	res = pkg_repo_binary_update_proceed(filepath, repo, &t, force);
 	if (res != EPKG_OK && res != EPKG_UPTODATE) {
 		pkg_emit_notice("Unable to update repository %s", repo->name);
@@ -640,6 +651,16 @@ pkg_repo_binary_update(struct pkg_repo *repo, bool for
 	}
 
 cleanup:
+	if (ld != -1) {
+		flock(ld, LOCK_UN);
+		close(ld);
+	}
+
+	if (lockpath != NULL) {
+		unlink(lockpath);
+		free(lockpath);
+	}
+
 	/* Set mtime from http request if possible */
 	if (t != 0 && res == EPKG_OK) {
 		struct timeval ftimes[2] = {
