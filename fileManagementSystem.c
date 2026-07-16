#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_USERNAME 32
#define MAX_PASSWORD 32
#define MAX_FILENAME 64
#define MAX_BUFFER 1024
#define KEY_SIZE 16

// User definition
typedef struct {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD]; 
    int uid;                     
    int gid;                     
} User;

// Active session state
typedef struct {
    User current_user;
    int is_logged_in;
} Session;

Session session = { .is_logged_in = 0 };

User user_db[] = {
    {"admin", "admin123", 1000, 1000},
    {"pradip", "pradip456", 1001, 1001},
    {"ram", "ram789", 1002, 1001} 
};
int num_users = 3;

// --- Security Helper Functions ---

int is_safe_filename(const char *filename) {
    if (filename == NULL || strlen(filename) == 0) return 0;
    if (strstr(filename, "..") != NULL || strchr(filename, '/') != NULL || strchr(filename, '\\') != NULL) {
        return 0; 
    }
    return 1;
}

void write_audit_log(const char *username, const char *action, const char *filename, int success) {
    FILE *log_file = fopen("audit.log", "a");
    if (!log_file) {
        perror("Critical Error: Unable to open audit log");
        return;
    }
    
    time_t now;
    time(&now);
    char *date = ctime(&now);
    date[strlen(date) - 1] = '\0'; 

    fprintf(log_file, "[%s] User: %s | Action: %s | Target: %s | Status: %s\n",
            date, username, action, filename, success ? "SUCCESS" : "FAILED");
    fclose(log_file);
}

void sanitize_input(char *str, int max_len) {
    str[strcspn(str, "\r\n")] = '\0';
}

// --- Authentication ---

int authenticate_user(const char *username, const char *password) {
    for (int i = 0; i < num_users; i++) {
        if (strcmp(user_db[i].username, username) == 0 && strcmp(user_db[i].password, password) == 0) {
            session.current_user = user_db[i];
            session.is_logged_in = 1;
            write_audit_log(username, "LOGIN", "N/A", 1);
            return 1;
        }
    }
    write_audit_log(username, "LOGIN_FAILED", "N/A", 0);
    return 0;
}

// --- File Permissions Logic ---

int check_permission(const char *filename, int requested_mode) {
    if (strcmp(session.current_user.username, "admin") == 0) {
        return 1; 
    }

    struct stat file_stat;
    if (stat(filename, &file_stat) < 0) {
        return 0; 
    }

    uid_t file_uid = file_stat.st_uid;
    gid_t file_gid = file_stat.st_gid;
    mode_t file_mode = file_stat.st_mode;

    // 1. Owner check
    if (session.current_user.uid == file_uid) {
        if (requested_mode & 4) return (file_mode & S_IRUSR) ? 1 : 0;
        if (requested_mode & 2) return (file_mode & S_IWUSR) ? 1 : 0;
        if (requested_mode & 1) return (file_mode & S_IXUSR) ? 1 : 0;
    }
    // 2. Group check
    else if (session.current_user.gid == file_gid) {
        if (requested_mode & 4) return (file_mode & S_IRGRP) ? 1 : 0;
        if (requested_mode & 2) return (file_mode & S_IWGRP) ? 1 : 0;
        if (requested_mode & 1) return (file_mode & S_IXGRP) ? 1 : 0;
    }
    // 3. Others check
    else {
        if (requested_mode & 4) return (file_mode & S_IROTH) ? 1 : 0;
        if (requested_mode & 2) return (file_mode & S_IWOTH) ? 1 : 0;
        if (requested_mode & 1) return (file_mode & S_IXOTH) ? 1 : 0;
    }

    return 0;
}

// --- File Operations ---

void create_file() {
    char filename[MAX_FILENAME];
    int octal_permissions;

    printf("Enter filename to create: ");
    fgets(filename, MAX_FILENAME, stdin);
    sanitize_input(filename, MAX_FILENAME);

    if (!is_safe_filename(filename)) {
        printf("Error: Invalid or dangerous filename.\n");
        write_audit_log(session.current_user.username, "CREATE_FILE_REJECTED", filename, 0);
        return;
    }

    printf("Enter POSIX permissions (e.g., 0640, 0700): ");
    if (scanf("%o", &octal_permissions) != 1) {
        printf("Invalid permissions format.\n");
        while (getchar() != '\n'); 
        return;
    }
    while (getchar() != '\n'); 

    FILE *f = fopen(filename, "w");
    if (f == NULL) {
        perror("Error creating file");
        write_audit_log(session.current_user.username, "CREATE_FILE", filename, 0);
        return;
    }
    fclose(f);

    if (chmod(filename, octal_permissions) < 0) {
        perror("Error setting permissions");
    }
    if(chown(filename,
         session.current_user.uid,
         session.current_user.gid)<0){
    perror("Warning: chown failed");
    }

    printf("File '%s' created successfully with permissions %o.\n", filename, octal_permissions);
    write_audit_log(session.current_user.username, "CREATE_FILE", filename, 1);
}

void read_file() {
    char filename[MAX_FILENAME];
    char buffer[MAX_BUFFER];

    printf("Enter filename to read: ");
    fgets(filename, MAX_FILENAME, stdin);
    sanitize_input(filename, MAX_FILENAME);

    if (!is_safe_filename(filename)) {
        printf("Error: Invalid or dangerous filename.\n");
        return;
    }

    if (!check_permission(filename, 4)) { 
        printf("Access Denied: You do not have Read permission.\n");
        write_audit_log(session.current_user.username, "READ_FILE_DENIED", filename, 0);
        return;
    }

    FILE *f = fopen(filename, "r");
    if (f == NULL) {
        perror("Error opening file");
        return;
    }

    printf("\n--- File Contents ---\n");
    while (fgets(buffer, sizeof(buffer), f) != NULL) {
        printf("%s", buffer);
    }
    printf("\n---------------------\n");
    fclose(f);

    write_audit_log(session.current_user.username, "READ_FILE", filename, 1);
}

void write_file() {
    char filename[MAX_FILENAME];
    char buffer[MAX_BUFFER];

    printf("Enter filename to write to: ");
    fgets(filename, MAX_FILENAME, stdin);
    sanitize_input(filename, MAX_FILENAME);

    if (!is_safe_filename(filename)) {
        printf("Error: Invalid or dangerous filename.\n");
        return;
    }

    // First check if the file exists
    if (access(filename, F_OK) != 0) {
    printf("Error: File '%s' does not exist.\n", filename);
    write_audit_log(session.current_user.username, "WRITE_FILE_FAILED", filename, 0);
    return;
    }

    // Then check write permission
    if (!check_permission(filename, 2)) {
    printf("Access Denied: You do not have Write permission.\n");
    write_audit_log(session.current_user.username, "WRITE_FILE_DENIED", filename, 0);
    return;
    }

    FILE *f = fopen(filename, "a");
    if (f == NULL) {
    perror("Error opening file");
    write_audit_log(session.current_user.username, "WRITE_FILE_FAILED", filename, 0);
    return;
    }

    printf("Enter text to write (Max %d chars):\n", MAX_BUFFER);
    fgets(buffer, MAX_BUFFER, stdin);
    
    fprintf(f, "%s", buffer);
    fclose(f);

    printf("Data successfully appended.\n");
    write_audit_log(session.current_user.username, "WRITE_FILE", filename, 1);
}

void delete_file() {
    char filename[MAX_FILENAME];
    printf("Enter filename to delete: ");
    fgets(filename, MAX_FILENAME, stdin);
    sanitize_input(filename, MAX_FILENAME);

    if (!is_safe_filename(filename)) {
        printf("Error: Invalid or dangerous filename.\n");
        return;
    }

    if (!check_permission(filename, 2)) {
        printf("Access Denied: You do not have Write permission required to delete.\n");
        write_audit_log(session.current_user.username, "DELETE_FILE_DENIED", filename, 0);
        return;
    }

    if (remove(filename) == 0) {
        printf("File '%s' deleted successfully.\n", filename);
        write_audit_log(session.current_user.username, "DELETE_FILE", filename, 1);
    } else {
        perror("Error deleting file");
        write_audit_log(session.current_user.username, "DELETE_FILE_FAILED", filename, 0);
    }
}

/* NEW: Execute File Feature */
void execute_file() {
    char filename[MAX_FILENAME];
    printf("Enter executable/script filename to run: ");
    fgets(filename, MAX_FILENAME, stdin);
    sanitize_input(filename, MAX_FILENAME);

    if (!is_safe_filename(filename)) {
        printf("Error: Invalid or dangerous filename.\n");
        return;
    }

    // Check for Execute permission (1)
    if (!check_permission(filename, 1)) {
        printf("Access Denied: You do not have Execute permission.\n");
        write_audit_log(session.current_user.username, "EXECUTE_FILE_DENIED", filename, 0);
        return;
    }

    printf("Executing dynamic path: ./%s\n", filename);
    char command[MAX_FILENAME + 3];
    snprintf(command, sizeof(command), "./%s", filename);

    // Run safe system call
    int ret = system(command);
    if (ret == -1) {
        perror("Error executing file");
        write_audit_log(session.current_user.username, "EXECUTE_FILE_FAILED", filename, 0);
    } else {
        printf("Execution completed with exit code %d.\n", ret);
        write_audit_log(session.current_user.username, "EXECUTE_FILE", filename, 1);
    }
}

// --- Cryptographic Module ---

void xor_cipher(const char *input_file, const char *output_file, const char *key) {
    FILE *fin = fopen(input_file, "rb");
    FILE *fout = fopen(output_file, "wb");
    if (!fin || !fout) {
        perror("Cryptographic Error: Unable to open file streams");
        if (fin) fclose(fin);
        if (fout) fclose(fout);
        return;
    }

    int key_len = strlen(key);
    int key_idx = 0;
    int ch;

    while ((ch = fgetc(fin)) != EOF) {
        fputc(ch ^ key[key_idx], fout);
        key_idx = (key_idx + 1) % key_len;
    }

    fclose(fin);
    fclose(fout);
}

void handle_encryption_decryption(int encrypt) {
    char source_file[MAX_FILENAME];
    char target_file[MAX_FILENAME];
    char key[KEY_SIZE];

    printf("Enter source filename: ");
    fgets(source_file, MAX_FILENAME, stdin);
    sanitize_input(source_file, MAX_FILENAME);

    if (!is_safe_filename(source_file)) {
        printf("Error: Invalid source filename.\n");
        return;
    }

   if (!check_permission(source_file, 4)) {
        printf("Access Denied: Insufficient permissions for crypto operations.\n");
        return;
    }

    printf("Enter target filename (for output): ");
    fgets(target_file, MAX_FILENAME, stdin);
    sanitize_input(target_file, MAX_FILENAME);

    if (!is_safe_filename(target_file)) {
        printf("Error: Invalid target filename.\n");
        return;
    }

    printf("Enter cryptographic key (Max 15 chars): ");
    fgets(key, KEY_SIZE, stdin);
    sanitize_input(key, KEY_SIZE);

    if (strlen(key) < 4) {
        printf("Error: Password key is too weak (Must be at least 4 characters).\n");
        return;
    }

    xor_cipher(source_file, target_file, key);

    // Copy the source file permissions to the new file
    struct stat st;
    if (stat(source_file, &st) == 0) {
        chmod(target_file, st.st_mode & 0777);
    }

    printf("Operation successfully completed. File written to '%s'.\n", target_file);

    write_audit_log(
        session.current_user.username,
        encrypt ? "ENCRYPT_FILE" : "DECRYPT_FILE",
        source_file,
        1
    );
}

// --- Menu System ---

int main() {
    char username[MAX_USERNAME];
    char password[MAX_PASSWORD];

    printf("==========================================\n");
    printf("      SECURE FILE SYSTEM OPERATIONS       \n");
    printf("==========================================\n");

    int login_attempts = 0;
    while (!session.is_logged_in) {
        if (login_attempts >= 3) {
            printf("Too many failed attempts. Locking system.\n");
            return 1;
        }
        printf("Username: ");
        fgets(username, MAX_USERNAME, stdin);
        sanitize_input(username, MAX_USERNAME);

        printf("Password: ");
        fgets(password, MAX_PASSWORD, stdin);
        sanitize_input(password, MAX_PASSWORD);

        if (!authenticate_user(username, password)) {
            printf("Invalid credentials. Please try again.\n");
            login_attempts++;
        } else {
            printf("\nWelcome, %s! Access Granted.\n", session.current_user.username);
        }
    }

    int choice;
    while (1) {
        printf("\n--- Secure File System Panel ---\n");
        printf("1. Create File\n");
        printf("2. Read File\n");
        printf("3. Append Write to File\n");
        printf("4. Delete File\n");
        printf("5. Encrypt File\n");
        printf("6. Decrypt File\n");
        printf("7. Execute File\n"); // NEW Menu addition
        printf("8. Exit System\n");
        printf("Enter action option (1-8): ");
        
        if (scanf("%d", &choice) != 1) {
            printf("Invalid choice. Please enter a number.\n");
            while (getchar() != '\n'); 
            continue;
        }
        while (getchar() != '\n'); 

        switch (choice) {
            case 1: create_file(); break;
            case 2: read_file(); break;
            case 3: write_file(); break;
            case 4: delete_file(); break;
            case 5: handle_encryption_decryption(1); break;
            case 6: handle_encryption_decryption(0); break;
            case 7: execute_file(); break; 
            case 8:
                printf("Exiting system. Audit logging session termination...\n");
                write_audit_log(session.current_user.username, "LOGOUT", "N/A", 1);
                exit(0);
            default:
                printf("Invalid selection.\n");
        }
    }
    return 0;
}