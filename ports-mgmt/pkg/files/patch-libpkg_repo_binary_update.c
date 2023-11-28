--- libpkg/repo/binary/update.c.orig	2023-06-29 13:25:53 UTC
+++ libpkg/repo/binary/update.c
@@ -26,6 +26,7 @@
 
 #include <sys/stat.h>
 #include <sys/param.h>
+#include <sys/file.h>
 #include <sys/time.h>
 
 #include <stdio.h>
@@ -465,9 +466,9 @@ pkg_repo_binary_update_proceed(const char *name, struc
 	struct pkg_manifest_key *keys = NULL;
 	size_t len = 0;
 	bool in_trans = false;
-	char *path = NULL;
+	char *path = NULL, *lockpath = NULL;
 	FILE *f = NULL;
-	int fd;
+	int fd, ld = -1;
 	char *line = NULL;
 	size_t linecap = 0;
 	ssize_t linelen, totallen = 0;
@@ -490,6 +491,16 @@ pkg_repo_binary_update_proceed(const char *name, struc
 		repo->meta->manifests, &local_t, &rc, &len);
 	if (fd == -1)
 		goto cleanup;
+
+	xasprintf(&lockpath, "%s-lock", name);
+	ld = open(lockpath, O_CREAT|O_TRUNC|O_WRONLY, 00644);
+	if (flock(ld, LOCK_EX|LOCK_NB) == -1) {
+		pkg_emit_error("repository %s could not be locked for update",
+			repo->name);
+		rc = EPKG_FATAL;
+		goto cleanup;
+	}
+
 	f = fdopen(fd, "r");
 	rewind(f);
 
@@ -555,7 +566,16 @@ pkg_repo_binary_update_proceed(const char *name, struc
 	 );
 
 cleanup:
+	if (ld != -1) {
+		flock(ld, LOCK_UN);
+		close(ld);
+	}
 
+	if (lockpath != NULL) {
+		unlink(lockpath);
+		free(lockpath);
+	}
+
 	if (in_trans) {
 		if (rc != EPKG_OK)
 			pkgdb_transaction_rollback_sqlite(sqlite, "REPO");
@@ -564,7 +584,7 @@ cleanup:
 			rc = EPKG_FATAL;
 	}
 	/* restore the previous db in case of failures */
-	if (rc != EPKG_OK && rc != EPKG_UPTODATE) {
+	if (rc != EPKG_OK && rc != EPKG_UPTODATE && path != NULL) {
 		unlink(name);
 		rename(path, name);
 	}
