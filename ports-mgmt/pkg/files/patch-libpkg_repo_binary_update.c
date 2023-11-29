--- libpkg/repo/binary/update.c.orig	2023-06-29 13:25:53 UTC
+++ libpkg/repo/binary/update.c
@@ -26,6 +26,7 @@
 
 #include <sys/stat.h>
 #include <sys/param.h>
+#include <sys/file.h>
 #include <sys/time.h>
 
 #include <stdio.h>
@@ -586,13 +587,15 @@ int
 pkg_repo_binary_update(struct pkg_repo *repo, bool force)
 {
 	char filepath[MAXPATHLEN];
+	char *lockpath = NULL;
 	const char update_finish_sql[] = ""
 		"DROP TABLE repo_update;";
+	const char *filename;
 	sqlite3 *sqlite;
 
 	struct stat st;
 	time_t t = 0;
-	int res = EPKG_FATAL;
+	int dfd, ld, res = EPKG_FATAL;
 
 	bool got_meta = false;
 
@@ -601,32 +604,47 @@ pkg_repo_binary_update(struct pkg_repo *repo, bool for
 	if (!pkg_repo_enabled(repo))
 		return (EPKG_OK);
 
-	pkg_debug(1, "PkgRepo: verifying update for %s", pkg_repo_name(repo));
+	pkg_debug(1, "PkgRepo: verifying update for %s", repo->name);
 
+	filename = pkg_repo_binary_get_filename(repo->name);
+
 	/* First of all, try to open and init repo and check whether it is fine */
 	if (repo->ops->open(repo, R_OK|W_OK) != EPKG_OK) {
-		pkg_debug(1, "PkgRepo: need forced update of %s", pkg_repo_name(repo));
+		pkg_debug(1, "PkgRepo: need forced update of %s", repo->name);
 		t = 0;
 		force = true;
 		snprintf(filepath, sizeof(filepath), "%s/%s", ctx.dbdir,
-		    pkg_repo_binary_get_filename(pkg_repo_name(repo)));
+		    filename);
 	}
 	else {
 		repo->ops->close(repo, false);
-		snprintf(filepath, sizeof(filepath), "%s/%s.meta", ctx.dbdir, pkg_repo_name(repo));
+		snprintf(filepath, sizeof(filepath), "%s/%s.meta", ctx.dbdir, repo->name);
 		if (stat(filepath, &st) != -1) {
 			t = force ? 0 : st.st_mtime;
 			got_meta = true;
 		}
 
 		snprintf(filepath, sizeof(filepath), "%s/%s", ctx.dbdir,
-			pkg_repo_binary_get_filename(pkg_repo_name(repo)));
+			filename);
 		if (got_meta && stat(filepath, &st) != -1) {
 			if (!force)
 				t = st.st_mtime;
 		}
 	}
 
+	xasprintf(&lockpath, "%s-lock", filename);
+	dfd = pkg_get_dbdirfd();
+	ld = openat(dfd, lockpath, O_CREAT|O_TRUNC|O_WRONLY, 00644);
+	if (flock(ld, LOCK_EX|LOCK_NB) == -1) {
+		/* lock blocking anyway to let the other end finish */
+		pkg_emit_notice("Waiting for another process to "
+		    "update repository %s", repo->name);
+		flock(ld, LOCK_EX);
+		res = EPKG_OK;
+		t = 0;
+		goto cleanup;
+	}
+
 	res = pkg_repo_binary_update_proceed(filepath, repo, &t, force);
 	if (res != EPKG_OK && res != EPKG_UPTODATE) {
 		pkg_emit_notice("Unable to update repository %s", repo->name);
@@ -640,6 +658,16 @@ pkg_repo_binary_update(struct pkg_repo *repo, bool for
 	}
 
 cleanup:
+	if (ld != -1) {
+		flock(ld, LOCK_UN);
+		close(ld);
+	}
+
+	if (lockpath != NULL) {
+		unlinkat(dfd, lockpath, 0);
+		free(lockpath);
+	}
+
 	/* Set mtime from http request if possible */
 	if (t != 0 && res == EPKG_OK) {
 		struct timeval ftimes[2] = {
@@ -655,7 +683,7 @@ cleanup:
 
 		utimes(filepath, ftimes);
 		if (got_meta) {
-			snprintf(filepath, sizeof(filepath), "%s/%s.meta", ctx.dbdir, pkg_repo_name(repo));
+			snprintf(filepath, sizeof(filepath), "%s/%s.meta", ctx.dbdir, repo->name);
 			utimes(filepath, ftimes);
 		}
 	}
