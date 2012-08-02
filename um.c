/*
    um.c -- User Management

    User Management routines for adding/deleting/changing users and groups
    Also, routines for determining user access

    Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/

#include    "goahead.h"

#if BIT_USER_MANAGEMENT
/********************************** Defines ***********************************/

#define UM_DB_FILENAME  T("um.xml")
#define UM_TXT_FILENAME T("umconfig.txt")

/*
 *  Table names
 */
#define UM_USER_TABLENAME   T("users")
#define UM_GROUP_TABLENAME  T("groups")
#define UM_ACCESS_TABLENAME T("access")

/*
 *  Column names
 */
#define UM_NAME         T("name")
#define UM_PASS         T("password")
#define UM_GROUP        T("group")
#define UM_PROT         T("prot")
#define UM_DISABLE      T("disable")
#define UM_METHOD       T("method")
#define UM_PRIVILEGE    T("priv")
#define UM_SECURE       T("secure")

/*
    XOR encryption mask
    Note:This string should be modified for individual sites
        in order to enhance user password security.
    MOB - generate or move to config.
 */
#define UM_XOR_ENCRYPT  T("*j7a(L#yZ98sSd5HfSgGjMj8;Ss;d)(*&^#@$a2s0i3g")

#define     NONE_OPTION     T("<NONE>")
#define     MSG_START       T("<body><h2>")
#define     MSG_END         T("</h2></body>")

/********************************** Defines ***********************************/
/*
    User table definition
 */
#define NUMBER_OF_USER_COLUMNS  5

//  MOB - should these be static
char_t  *userColumnNames[NUMBER_OF_USER_COLUMNS] = {
        UM_NAME, UM_PASS, UM_GROUP, UM_PROT, UM_DISABLE
};

int     userColumnTypes[NUMBER_OF_USER_COLUMNS] = {
        T_STRING, T_STRING, T_STRING, T_INT, T_INT
};

dbTable_t userTable = {
    UM_USER_TABLENAME,
    NUMBER_OF_USER_COLUMNS,
    userColumnNames,
    userColumnTypes,
    0,
    NULL
};

/*
    Group table definition
 */
#define NUMBER_OF_GROUP_COLUMNS 5

char_t  *groupColumnNames[NUMBER_OF_GROUP_COLUMNS] = {
        UM_NAME, UM_PRIVILEGE, UM_METHOD, UM_PROT, UM_DISABLE
};

int     groupColumnTypes[NUMBER_OF_GROUP_COLUMNS] = {
    T_STRING, T_INT, T_INT, T_INT, T_INT
};

dbTable_t groupTable = {
    UM_GROUP_TABLENAME,
    NUMBER_OF_GROUP_COLUMNS,
    groupColumnNames,
    groupColumnTypes,
    0,
    NULL
};

/*
 *  Access Limit table definition
 */
#define NUMBER_OF_ACCESS_COLUMNS    4

char_t  *accessColumnNames[NUMBER_OF_ACCESS_COLUMNS] = {
    UM_NAME, UM_METHOD, UM_SECURE, UM_GROUP
};

int     accessColumnTypes[NUMBER_OF_ACCESS_COLUMNS] = {
    T_STRING, T_INT, T_INT, T_STRING
};

dbTable_t accessTable = {
    UM_ACCESS_TABLENAME,
    NUMBER_OF_ACCESS_COLUMNS,
    accessColumnNames,
    accessColumnTypes,
    0,
    NULL
};

/* 
    Database Identifier returned from dbOpen()
 */
static int  didUM = -1; 

/* 
    Configuration database persist filename
 */
static char_t   *saveFilename = NULL;

static int      umOpenCount = 0;        /* count of apps using this module */

/********************************** Forwards **********************************/

static int      aspGenerateUserList(int eid, webs_t wp, int argc, char_t **argv);
static int      aspGenerateGroupList(int eid, webs_t wp, int argc, char_t **argv);
static int      aspGenerateAccessLimitList(int eid, webs_t wp, int argc, char_t **argv);
static int      aspGenerateAccessMethodList(int eid, webs_t wp, int argc, char_t **argv);
static int      aspGeneratePrivilegeList(int eid, webs_t wp, int argc, char_t **argv);
static void     formAddUser(webs_t wp, char_t *path, char_t *query);
static void     formDeleteUser(webs_t wp, char_t *path, char_t *query);
static void     formDisplayUser(webs_t wp, char_t *path, char_t *query);
static void     formAddGroup(webs_t wp, char_t *path, char_t *query);
static void     formDeleteGroup(webs_t wp, char_t *path, char_t *query);
static void     formAddAccessLimit(webs_t wp, char_t *path, char_t *query);
static void     formDeleteAccessLimit(webs_t wp, char_t *path, char_t *query);
static void     formSaveUserManagement(webs_t wp, char_t *path, char_t *query);
static void     formLoadUserManagement(webs_t wp, char_t *path, char_t *query);
static bool_t   umCheckName(char_t *name);

/*********************************** Code *************************************/
/*
 *  umOpen() registers the UM tables in the fake emf-database 
 */

int umOpen()
{
    if (++umOpenCount != 1) {
        return didUM;
    }
/*
 *  Do not initialize if intialization has already taken place
 */
    if (didUM == -1) {
        didUM = dbOpen(UM_USER_TABLENAME, UM_DB_FILENAME, NULL, 0);
        dbRegisterDBSchema(&userTable);
        dbRegisterDBSchema(&groupTable);
        dbRegisterDBSchema(&accessTable);
    }
    if (saveFilename == NULL) {
        saveFilename = bstrdup(UM_TXT_FILENAME);
    }
    return didUM;
}


void umClose()
{
    if (--umOpenCount > 0) {
        return;
    }
    /*
        Do not close if intialization has not taken place
     */
    if (didUM != -1) {
        dbClose(didUM);
        didUM = -1;
    }
    if (saveFilename != NULL) {
        bfree(saveFilename);
        saveFilename = NULL;
    }
}


/*
    umCommit() persists all of the UM tables
 */
int umCommit(char_t *filename)
{
    if (filename && *filename) {
        if (saveFilename != NULL) {
            bfree(saveFilename);
        }
        saveFilename = bstrdup(filename);
    }
    a_assert (saveFilename && *saveFilename);
    trace(3, T("UM: Writing User Configuration to file <%s>\n"), saveFilename);
    return dbSave(didUM, saveFilename, 0);
}

/*
    umRestore() loads up the UM tables with persisted data
 */
int umRestore(char_t *filename)
{
    if (filename && *filename) {
        if (saveFilename != NULL) {
            bfree(saveFilename);
        }
        saveFilename = bstrdup(filename);
    }
    a_assert(saveFilename && *saveFilename);
    trace(3, T("UM: Loading User Configuration from file <%s>\n"), saveFilename);
    /*
        First empty the database, otherwise we wind up with duplicates!
     */
    dbZero(didUM);
    return dbLoad(didUM, saveFilename, 0);
}


/*
    Encrypt/Decrypt a text string. Returns the number of characters encrypted.
 */
static int umEncryptString(char_t *textString)
{
    char_t  *enMask;
    char_t  enChar;
    int     numChars;

    a_assert(textString);

    enMask = UM_XOR_ENCRYPT;
    numChars = 0;

    while (*textString) {
        enChar = *textString ^ *enMask;
        /*
            Do not produce encrypted text with embedded linefeeds or tabs. Simply use existing character.
         */
        if (enChar && !gisspace(enChar)) {
            *textString = enChar;
        }
        /*
            Increment all pointers.
         */
        enMask++;
        textString++;
        numChars++;
        /*
            Wrap encryption mask pointer if at end of length.
         */
        if (*enMask == '\0') {
            enMask = UM_XOR_ENCRYPT;
        }
    }
    return numChars;
}

/*
    umGetFirstRowData() -   return a pointer to the first non-blank key value
                            in the given column for the given table.
 */
static char_t *umGetFirstRowData(char_t *tableName, char_t *columnName)
{
    char_t  *columnData;
    int     row;
    int     check;

    a_assert(tableName && *tableName);
    a_assert(columnName && *columnName);

    row = 0;
    /*
        Move through table until we retrieve the first row with non-null column data.
     */
    columnData = NULL;
    while ((check = dbReadStr(didUM, tableName, columnName, row++, 
        &columnData)) == 0 || (check == DB_ERR_ROW_DELETED)) {
        if (columnData && *columnData) {
            return columnData;
        }
    }

    return NULL;
}


/*
    umGetNextRowData() -    return a pointer to the first non-blank 
                            key value following the given one.
 */

static char_t *umGetNextRowData(char_t *tableName, char_t *columnName, 
                                char_t *keyLast)
{
    char_t  *key;
    int     row;
    int     check;

    a_assert(tableName && *tableName);
    a_assert(columnName && *columnName);
    a_assert(keyLast && *keyLast);

    /*
        Position row counter to row where the given key value was found
     */
    row = 0;
    key = NULL;

    while ((((check = dbReadStr(didUM, tableName, columnName, row++, 
        &key)) == 0) || (check == DB_ERR_ROW_DELETED)) &&
        ((key == NULL) || (gstrcmp(key, keyLast) != 0))) {
    }
    /*
        If the last key value was not found, return NULL
     */
    if (!key || gstrcmp(key, keyLast) != 0) {
        return NULL;
    }
    /*
        Move through table until we retrieve the next row with a non-null key
     */
    while (((check = dbReadStr(didUM, tableName, columnName, row++, &key)) 
        == 0) || (check == DB_ERR_ROW_DELETED)) {
        if (key && *key && (gstrcmp(key, keyLast) != 0)) {
            return key;
        }
    }

    return NULL;
}


/*
    umAddUser() - Adds a user to the "users" table.
 */
int umAddUser(char_t *user, char_t *pass, char_t *group, bool_t prot, bool_t disabled)
{
    int     row;
    char_t  *password;

    a_assert(user && *user);
    a_assert(pass && *pass);
    a_assert(group && *group);

    trace(3, T("UM: Adding User <%s>\n"), user);

    /*
        Do not allow duplicates
     */
    if (umUserExists(user)) {
        return UM_ERR_DUPLICATE;
    }

    /*
        Make sure user name and password contain valid characters
     */
    if (!umCheckName(user)) {
        return UM_ERR_BAD_NAME;
    }

    if (!umCheckName(pass)) {
        return UM_ERR_BAD_PASSWORD;
    }

    /*
        Make sure group exists
     */
    if (!umGroupExists(group)) {
        return UM_ERR_NOT_FOUND;
    }

    /*
        Now create the user record
     */
    row = dbAddRow(didUM, UM_USER_TABLENAME);

    if (row < 0) {
        return UM_ERR_GENERAL;
    }
    if (dbWriteStr(didUM, UM_USER_TABLENAME, UM_NAME, row, user) != 0) {
        return UM_ERR_GENERAL;
    }
    password = bstrdup(pass);
    umEncryptString(password);
    dbWriteStr(didUM, UM_USER_TABLENAME, UM_PASS, row, password);
    bfree(password);
    dbWriteStr(didUM, UM_USER_TABLENAME, UM_GROUP, row, group);
    dbWriteInt(didUM, UM_USER_TABLENAME, UM_PROT, row, prot); 
    dbWriteInt(didUM, UM_USER_TABLENAME, UM_DISABLE, row, disabled);

    return 0;
}


/*
    umDeleteUser() - remove a user from the "users" table
 */
int umDeleteUser(char_t *user)
{
    int row;

    a_assert(user && *user);
    trace(3, T("UM: Deleting User <%s>\n"), user);
    /*
        Check to see if user is delete-protected
     */
    if (umGetUserProtected(user)) {
        return UM_ERR_PROTECTED;
    } 
    /*
        If found, delete the user from the database
     */
    if ((row = dbSearchStr(didUM, UM_USER_TABLENAME, UM_NAME, user, 0)) >= 0) {
        return dbDeleteRow(didUM, UM_USER_TABLENAME, row);
    } 
    return UM_ERR_NOT_FOUND;
}


/*
    umGetFirstUser() -  Returns the user ID of the first user found in the
                        "users" table.
 */
char_t *umGetFirstUser()
{
    return umGetFirstRowData(UM_USER_TABLENAME, UM_NAME);
}


/*
    umGetNextUser() Returns the next user found in the "users" table after
                    the given user.     
 */
char_t *umGetNextUser(char_t *userLast)
{
    return umGetNextRowData(UM_USER_TABLENAME, UM_NAME, userLast);
}


/*
    umUserExists()  Returns TRUE if userid exists.
 */
bool_t umUserExists(char_t *user)
{
    a_assert(user && *user);
    return dbSearchStr(didUM, UM_USER_TABLENAME, UM_NAME, user, 0) >= 0;
}


/*
    umGetUserPassword() returns a de-crypted copy of the user password
 */
char_t *umGetUserPassword(char_t *user)
{
    char_t  *password;
    int     row;

    a_assert(user && *user);

    password = NULL;
    row = dbSearchStr(didUM, UM_USER_TABLENAME, UM_NAME, user, 0);

    if (row >= 0) {
        char_t *pass = NULL;
        dbReadStr(didUM, UM_USER_TABLENAME, UM_PASS, row, &pass);
        /*
            Decrypt password. Note, this function returns a copy of the password, which must be deleted at some time in
            the future.
         */
        password = bstrdup(pass);
        umEncryptString(password);
    }
    return password;
}

/*
    umSetUserPassword() updates the user password in the user "table" after
                        encrypting the given password
 */
int umSetUserPassword(char_t *user, char_t *pass)
{
    int     row, nRet;
    char_t  *password;

    a_assert(user && *user);
    a_assert(pass && *pass);
    trace(3, T("UM: Attempting to change the password for user <%s>\n"), user);

    /*
        Find the row of the user
     */
    if ((row = dbSearchStr(didUM, UM_USER_TABLENAME, UM_NAME, user, 0)) < 0) {
        return UM_ERR_NOT_FOUND;
    }
    password = bstrdup(pass);
    umEncryptString(password);
    nRet = dbWriteStr(didUM, UM_USER_TABLENAME, UM_PASS, row, password);
    bfree(password);

    return nRet;
}


/*
    umGetUserGroup() returns the name of the user group
 */
char_t *umGetUserGroup(char_t *user)
{
    char_t  *group;
    int     row;

    a_assert(user && *user);
    group = NULL;
    /*
        Find the row of the user
     */
    row = dbSearchStr(didUM, UM_USER_TABLENAME, UM_NAME, user, 0);

    if (row >= 0) {
        dbReadStr(didUM, UM_USER_TABLENAME, UM_GROUP, row, &group);
    }
    return group;
}


/*
    umSetUserGroup() Sets the name of the user group for the user
 */
int umSetUserGroup(char_t *user, char_t *group)
{
    int row;

    a_assert(user && *user);
    a_assert(group && *group);

    /*
        Find the row of the user
     */
    row = dbSearchStr(didUM, UM_USER_TABLENAME, UM_NAME, user, 0);
    if (row >= 0) {
        return dbWriteStr(didUM, UM_USER_TABLENAME, UM_GROUP, row, group);
    } else {
        return UM_ERR_NOT_FOUND;
    }
}

/*
    umGetUserEnabled() - returns if the user is enabled
    Returns FALSE if the user is not found.
 */
bool_t  umGetUserEnabled(char_t *user)
{
    int disabled, row;

    a_assert(user && *user);

    disabled = 1;
    /*
        Find the row of the user
     */
    row = dbSearchStr(didUM, UM_USER_TABLENAME, UM_NAME, user, 0);
    if (row >= 0) {
        dbReadInt(didUM, UM_USER_TABLENAME, UM_DISABLE, row, &disabled);
    }
    return (bool_t)!disabled;
}


/*
    umSetUserEnabled() Enables/disables the user
 */
int umSetUserEnabled(char_t *user, bool_t enabled)
{
    int row;

    a_assert(user && *user);
    /*
        Find the row of the user
     */
    row = dbSearchStr(didUM, UM_USER_TABLENAME, UM_NAME, user, 0);
    if (row >= 0) {
        return dbWriteInt(didUM, UM_USER_TABLENAME, UM_DISABLE, row, !enabled);
    } else {
        return UM_ERR_NOT_FOUND;
    }
}


/*
    umGetUserProtected() - determine deletability of user
 */
bool_t umGetUserProtected(char_t *user)
{
    int protect, row;

    a_assert(user && *user);
    /*
        Find the row of the user
     */
    row = dbSearchStr(didUM, UM_USER_TABLENAME, UM_NAME, user, 0);
    protect = 0;
    if (row >= 0) {
        dbReadInt(didUM, UM_USER_TABLENAME, UM_PROT, row, &protect);
    }
    return (bool_t) protect;
}


/*
    umSetUserProtected() sets the delete protection for the user
 */
int umSetUserProtected(char_t *user, bool_t protect)
{
    int row;

    a_assert(user && *user);
    /*
        Find the row of the user
     */
    row = dbSearchStr(didUM, UM_USER_TABLENAME, UM_NAME, user, 0);
    if (row >= 0) {
        return dbWriteInt(didUM, UM_USER_TABLENAME, UM_PROT, row, protect);
    } else {
        return UM_ERR_NOT_FOUND;
    }
}


/*
    umAddGroup() adds a group to the "Group" table
 */
int umAddGroup(char_t *group, short priv, accessMeth_t am, bool_t prot, bool_t disabled)
{
    int row;

    a_assert(group && *group);
    trace(3, T("UM: Adding group <%s>\n"), group);
    
    /*
        Do not allow duplicates
     */
    if (umGroupExists(group)) {
        return UM_ERR_DUPLICATE;
    }

    /*
        Only allow valid characters in key field
     */
    if (!umCheckName(group)) {
        return UM_ERR_BAD_NAME;
    }

    /*
        Add a new row to the table
     */
    if ((row = dbAddRow(didUM, UM_GROUP_TABLENAME)) < 0) {
        return UM_ERR_GENERAL;
    }

    /*
        Write the key field
     */
    if (dbWriteStr(didUM, UM_GROUP_TABLENAME, UM_NAME, row, group) != 0) {
        return UM_ERR_GENERAL;
    }

    /*
        Write the remaining fields
     */
    dbWriteInt(didUM, UM_GROUP_TABLENAME, UM_PRIVILEGE, row, priv);
    dbWriteInt(didUM, UM_GROUP_TABLENAME, UM_METHOD, row, (int) am);
    dbWriteInt(didUM, UM_GROUP_TABLENAME, UM_PROT, row, prot);
    dbWriteInt(didUM, UM_GROUP_TABLENAME, UM_DISABLE, row, disabled);
    return 0;
}


/*
    umDeleteGroup() - Delete a user group, if not protected
 */
int umDeleteGroup(char_t *group)
{
    int row;

    a_assert(group && *group);
    trace(3, T("UM: Deleting Group <%s>\n"), group);

    /*
        Check to see if the group is in use
     */
    if (umGetGroupInUse(group)) {
        return UM_ERR_IN_USE;
    } 

    /*
        Check to see if the group is delete-protected
     */
    if (umGetGroupProtected(group)) {
        return UM_ERR_PROTECTED;
    } 

    /*
        Find the row of the group to delete
     */
    if ((row = dbSearchStr(didUM, UM_GROUP_TABLENAME, UM_NAME, group, 0)) < 0) {
        return UM_ERR_NOT_FOUND;
    }

    return dbDeleteRow(didUM, UM_GROUP_TABLENAME, row);
}

/*
    umGroupExists() returns TRUE if group exists, FALSE otherwise
 */

bool_t umGroupExists(char_t *group)
{
    a_assert(group && *group);
    return dbSearchStr(didUM, UM_GROUP_TABLENAME, UM_NAME, group, 0) >= 0;
}


/*
    umGetGroupInUse() returns TRUE if the group is referenced by a user or by an access limit.
 */
bool_t umGetGroupInUse(char_t *group)
{
    a_assert(group && *group);

    /*
        First, check the user table
     */
    if (dbSearchStr(didUM, UM_USER_TABLENAME, UM_GROUP, group, 0) >= 0) {
        return 1;
    } 
    /*
        Second, check the access limit table
     */
    if (dbSearchStr(didUM, UM_ACCESS_TABLENAME, UM_GROUP, group, 0) >= 0) {
        return 1;
    } 
    return 0;
}


/*
    umGetFirstGroup() - return a pointer to the first non-blank group name
 */
char_t *umGetFirstGroup()
{
    return umGetFirstRowData(UM_GROUP_TABLENAME, UM_NAME);
}

/*
    umGetNextGroup() -  return a pointer to the first non-blank group name
                        following the given group name
 */
char_t *umGetNextGroup(char_t *groupLast)
{
    return umGetNextRowData(UM_GROUP_TABLENAME, UM_NAME, groupLast);
}


/*
    Returns the default access method to use for a given group
 */
accessMeth_t umGetGroupAccessMethod(char_t *group)
{
    int am, row;

    a_assert(group && *group);
    row = dbSearchStr(didUM, UM_GROUP_TABLENAME, UM_NAME, group, 0);
    if (row >= 0) {
        dbReadInt(didUM, UM_GROUP_TABLENAME, UM_METHOD, row, (int *)&am);
    } else {
        am = AM_INVALID;
    }
    return (accessMeth_t) am;
}


/*
    Set the default access method to use for a given group
 */
int umSetGroupAccessMethod(char_t *group, accessMeth_t am)
{
    int row;

    a_assert(group && *group);
    row = dbSearchStr(didUM, UM_GROUP_TABLENAME, UM_NAME, group, 0);
    if (row >= 0) {
        return dbWriteInt(didUM, UM_GROUP_TABLENAME, UM_METHOD, row, (int) am);
    } else {
        return UM_ERR_NOT_FOUND;
    }
}


/*
    Returns the privilege mask for a given group
 */
short umGetGroupPrivilege(char_t *group)
{
    int privilege, row;

    a_assert(group && *group);
    privilege = -1;
    row = dbSearchStr(didUM, UM_GROUP_TABLENAME, UM_NAME, group, 0);
    if (row >= 0) {
        dbReadInt(didUM, UM_GROUP_TABLENAME, UM_PRIVILEGE, row, &privilege);
    }
    return (short) privilege;
}


/*
    Set the privilege mask for a given group
 */
int umSetGroupPrivilege(char_t *group, short privilege)
{
    int row;

    a_assert(group && *group);
    row = dbSearchStr(didUM, UM_GROUP_TABLENAME, UM_NAME, group, 0);

    if (row >= 0) {
        return dbWriteInt(didUM, UM_GROUP_TABLENAME, UM_PRIVILEGE, row, (int)privilege);
    } else {
        return UM_ERR_NOT_FOUND;
    }
}


/*
    Returns the enabled setting for a given group. Returns FALSE if group is not found.
 */
bool_t umGetGroupEnabled(char_t *group)
{
    int disabled, row;

    a_assert(group && *group);
    row = dbSearchStr(didUM, UM_GROUP_TABLENAME, UM_NAME, group, 0);
    disabled = 1;

    if (row >= 0) {
        dbReadInt(didUM, UM_GROUP_TABLENAME, UM_DISABLE, row, &disabled);
    }
    return (bool_t) !disabled;
}


/*
    Sets the enabled setting for a given group.
 */
int umSetGroupEnabled(char_t *group, bool_t enabled)
{
    int row;

    a_assert(group && *group);
    row = dbSearchStr(didUM, UM_GROUP_TABLENAME, UM_NAME, group, 0);

    if (row >= 0) {
        return dbWriteInt(didUM, UM_GROUP_TABLENAME, UM_DISABLE, row, (int) !enabled);
    } else {
        return UM_ERR_NOT_FOUND;
    }
}


/*
    Returns the protected setting for a given group. Returns FALSE if user is not found
 */
bool_t umGetGroupProtected(char_t *group)
{
    int protect, row;

    a_assert(group && *group);

    protect = 0;
    row = dbSearchStr(didUM, UM_GROUP_TABLENAME, UM_NAME, group, 0);
    if (row >= 0) {
        dbReadInt(didUM, UM_GROUP_TABLENAME, UM_PROT, row, &protect);
    }
    return (bool_t) protect;
}


/*
    Sets the protected setting for a given group
 */
int umSetGroupProtected(char_t *group, bool_t protect)
{
    int row;

    a_assert(group && *group);
    row = dbSearchStr(didUM, UM_GROUP_TABLENAME, UM_NAME, group, 0);

    if (row >= 0) {
        return dbWriteInt(didUM, UM_GROUP_TABLENAME, UM_PROT, row, (int) protect);
    } else {
        return UM_ERR_NOT_FOUND;
    }
}


/*
    umAddAccessLimit() adds an access limit to the "access" table
 */
int umAddAccessLimit(char_t *url, accessMeth_t am, short secure, char_t *group)
{
    int row;

    a_assert(url && *url);
    trace(3, T("UM: Adding Access Limit for <%s>\n"), url);

    /*
        Do not allow duplicates
     */
    if (umAccessLimitExists(url)) {
        return UM_ERR_DUPLICATE;
    }

    /*
        Add a new row to the table
     */
    if ((row = dbAddRow(didUM, UM_ACCESS_TABLENAME)) < 0) {
        return UM_ERR_GENERAL;
    }

    /*
        Write the key field
     */
    if(dbWriteStr(didUM, UM_ACCESS_TABLENAME, UM_NAME, row, url) < 0) {
        return UM_ERR_GENERAL;
    }

    /*
        Write the remaining fields
     */
    dbWriteInt(didUM, UM_ACCESS_TABLENAME, UM_METHOD, row, (int)am);
    dbWriteInt(didUM, UM_ACCESS_TABLENAME, UM_SECURE, row, (int)secure);
    dbWriteStr(didUM, UM_ACCESS_TABLENAME, UM_GROUP, row, group);
    return 0;
}


/*
    umDeleteAccessLimit()
 */
int umDeleteAccessLimit(char_t *url)
{
    int row;

    a_assert(url && *url);
    trace(3, T("UM: Deleting Access Limit for <%s>\n"), url);
    /*
        Find the row of the access limit to delete
     */
    if ((row = dbSearchStr(didUM, UM_ACCESS_TABLENAME, UM_NAME, url, 0)) < 0) {
        return UM_ERR_NOT_FOUND;
    }
    return dbDeleteRow(didUM, UM_ACCESS_TABLENAME, row);
}


/*
    umGetFirstGroup() - return a pointer to the first non-blank access limit
 */
char_t *umGetFirstAccessLimit()
{
    return umGetFirstRowData(UM_ACCESS_TABLENAME, UM_NAME);
}


/*
    umGetNextAccessLimit() -    return a pointer to the first non-blank 
                                access limit following the given one
 */
char_t *umGetNextAccessLimit(char_t *urlLast)
{
    return umGetNextRowData(UM_ACCESS_TABLENAME, UM_NAME, urlLast);
}


/*
    umAccessLimitExists() returns TRUE if this access limit exists
 */
bool_t  umAccessLimitExists(char_t *url)
{
    a_assert(url && *url);
    return dbSearchStr(didUM, UM_ACCESS_TABLENAME, UM_NAME, url, 0) >= 0;
}

/*
    umGetAccessLimit() returns the Access Method for the URL
 */
accessMeth_t umGetAccessLimitMethod(char_t *url)
{
    int am, row;

    am = (int) AM_INVALID;
    row = dbSearchStr(didUM, UM_ACCESS_TABLENAME, UM_NAME, url, 0);

    if (row >= 0) {
        dbReadInt(didUM, UM_ACCESS_TABLENAME, UM_METHOD, row, &am);
    } 
    return (accessMeth_t) am;
}


/*
    umSetAccessLimitMethod() - set Access Method for Access Limit
 */
int umSetAccessLimitMethod(char_t *url, accessMeth_t am)
{
    int row;

    a_assert(url && *url);
    row = dbSearchStr(didUM, UM_ACCESS_TABLENAME, UM_NAME, url, 0);

    if (row >= 0) {
        return dbWriteInt(didUM, UM_ACCESS_TABLENAME, UM_METHOD, row, (int) am);
    } else {
        return UM_ERR_NOT_FOUND;
    }
}


/*
    umGetAccessLimitSecure() - returns secure switch for access limit
 */
short umGetAccessLimitSecure(char_t *url)
{
    int secure, row;

    a_assert(url && *url);
    secure = -1;
    row = dbSearchStr(didUM, UM_ACCESS_TABLENAME, UM_NAME, url, 0);

    if (row >= 0) {
        dbReadInt(didUM, UM_ACCESS_TABLENAME, UM_SECURE, row, &secure);
    }

    return (short)secure;
}


/*
    umSetAccessLimitSecure() - sets the secure flag for the URL
 */
int umSetAccessLimitSecure(char_t *url, short secure)
{
    int row;

    a_assert(url && *url);
    row = dbSearchStr(didUM, UM_ACCESS_TABLENAME, UM_NAME, url, 0);

    if (row >= 0) {
        return dbWriteInt(didUM, UM_ACCESS_TABLENAME, UM_SECURE, row, (int)secure);
    } else {
        return UM_ERR_NOT_FOUND;
    }
}


/*
    umGetAccessLimitGroup() - returns the user group of the access limit
 */
char_t *umGetAccessLimitGroup(char_t *url)
{
    char_t  *group;
    int     row;

    a_assert(url && *url);
    group = NULL;
    row = dbSearchStr(didUM, UM_ACCESS_TABLENAME, UM_NAME, url, 0);

    if (row >= 0) {
        dbReadStr(didUM, UM_ACCESS_TABLENAME, UM_GROUP, row, &group);
    }
    return group;
}


/*
    umSetAccessLimitGroup() - sets the user group for the access limit.
 */
int umSetAccessLimitGroup(char_t *url, char_t *group)
{
    int row;

    a_assert(url && *url);
    row = dbSearchStr(didUM, UM_ACCESS_TABLENAME, UM_NAME, url, 0);

    if (row >= 0) {
        return dbWriteStr(didUM, UM_ACCESS_TABLENAME, UM_GROUP, row, group);
    } else {
        return UM_ERR_NOT_FOUND;
    }
}


/*
    Returns the access limit to use for a given URL, by checking for URLs up the directory tree.  Creates a new string
    that must be deleted.  
 */
char_t *umGetAccessLimit(char_t *url)
{
    char_t  *urlRet, *urlCheck, *lastChar;
    int     len;
    
    a_assert(url && *url);
    urlRet = NULL;
    urlCheck = bstrdup(url);
    a_assert(urlCheck);
    len = gstrlen(urlCheck);
    /*
        Scan back through URL to see if there is a "parent" access limit
     */
    while (len && !urlRet) {
        if (umAccessLimitExists(urlCheck)) {
            urlRet = bstrdup(urlCheck);
        } else {
            /*
                Trim the end portion of the URL to the previous directory marker
             */
            lastChar = urlCheck + len;
            lastChar--;

            while ((lastChar >= urlCheck) && ((*lastChar == '/') || 
                (*lastChar == '\\'))) {
                *lastChar = 0;
                lastChar--;
            }
            while ((lastChar >= urlCheck) && (*lastChar != '/') && 
                (*lastChar != '\\')) {
                *lastChar = 0;
                lastChar--;
            }
            len = gstrlen(urlCheck);
        }
    }
    bfree (urlCheck);

    return urlRet;
}


/*
    Returns the access method to use for a given URL
 */
accessMeth_t umGetAccessMethodForURL(char_t *url)
{
    accessMeth_t    amRet;
    char_t          *urlHavingLimit, *group;
    
    urlHavingLimit = umGetAccessLimit(url);
    if (urlHavingLimit) {
        group = umGetAccessLimitGroup(urlHavingLimit);

        if (group && *group) {
            amRet = umGetGroupAccessMethod(group);
        } else {
            amRet = umGetAccessLimitMethod(urlHavingLimit);
        }
        bfree(urlHavingLimit);
    } else {
        amRet = AM_FULL;
    }
    return amRet;
}


/*
    Returns TRUE if user can access URL
 */
bool_t umUserCanAccessURL(char_t *user, char_t *url)
{
    accessMeth_t    amURL;
    char_t          *group, *usergroup, *urlHavingLimit;
    short           priv;
    
    a_assert(user && *user);
    a_assert(url && *url);

    /*
        Make sure user exists
     */
    if (!umUserExists(user)) {
        return 0;
    }

    /*
        Make sure user is enabled
     */
    if (!umGetUserEnabled(user)) {
        return 0;
    }

    /*
        Make sure user has sufficient privileges (any will do)
     */
    usergroup = umGetUserGroup(user);
    priv = umGetGroupPrivilege(usergroup);
    if (priv == 0) {
        return 0;
    }

    /*
        Make sure user's group is enabled
     */
    if (!umGetGroupEnabled(usergroup)) {
        return 0;
    }

    /*
        The access method of the user group must not be AM_NONE
     */
    if (umGetGroupAccessMethod(usergroup) == AM_NONE) {
        return 0;
    }

    /*
        Check to see if there is an Access Limit for this URL
     */
    urlHavingLimit = umGetAccessLimit(url);
    if (urlHavingLimit) {
        amURL = umGetAccessLimitMethod(urlHavingLimit);
        group = umGetAccessLimitGroup(urlHavingLimit);
        bfree(urlHavingLimit);
    } else {
        /*
            If there isn't an access limit for the URL, user has full access
         */
        return 1;
    }

    /*
        If the access method for the URL is AM_NONE then the file "doesn't exist".
     */
    if (amURL == AM_NONE) {
        return 0;
    } 
    
    /*
        If Access Limit has a group specified, then the user must be a member of that group
     */
    if (group && *group) {
        if (usergroup && (gstrcmp(group, usergroup) != 0)) {
            return 0;
        }
    } 
    /*
        Otherwise, user can access the URL 
     */
    return 1;

}


/*
    Returns TRUE if given name has only valid chars
 */
static bool_t umCheckName(char_t *name)
{
    a_assert(name && *name);

    if (name && *name) {
        while (*name) {
            if (gisspace(*name)) {
                return 0;
            }
            name++;
        }
        return 1;
    }
    return 0;
}


void formDefineUserMgmt(void)
{
    websAspDefine(T("MakeGroupList"), aspGenerateGroupList);
    websAspDefine(T("MakeUserList"), aspGenerateUserList);
    websAspDefine(T("MakeAccessLimitList"), aspGenerateAccessLimitList);
    websAspDefine(T("MakeAccessMethodList"), aspGenerateAccessMethodList);
    websAspDefine(T("MakePrivilegeList"), aspGeneratePrivilegeList);

    websFormDefine(T("AddUser"), formAddUser);
    websFormDefine(T("DeleteUser"), formDeleteUser);
    websFormDefine(T("DisplayUser"), formDisplayUser);
    websFormDefine(T("AddGroup"), formAddGroup);
    websFormDefine(T("DeleteGroup"), formDeleteGroup);
    websFormDefine(T("AddAccessLimit"), formAddAccessLimit);
    websFormDefine(T("DeleteAccessLimit"), formDeleteAccessLimit);

    websFormDefine(T("SaveUserManagement"), formSaveUserManagement);
    websFormDefine(T("LoadUserManagement"), formLoadUserManagement);
}


static void formAddUser(webs_t wp, char_t *path, char_t *query)
{
    char_t  *userid, *pass1, *pass2, *group, *enabled, *ok;
    bool_t bDisable;
    int nCheck;

    a_assert(wp);

    userid = websGetVar(wp, T("user"), T("")); 
    pass1 = websGetVar(wp, T("password"), T("")); 
    pass2 = websGetVar(wp, T("passconf"), T("")); 
    group = websGetVar(wp, T("group"), T("")); 
    enabled = websGetVar(wp, T("enabled"), T("")); 
    ok = websGetVar(wp, T("ok"), T("")); 

    websHeader(wp);
    websWrite(wp, MSG_START);

    if (gstricmp(ok, T("ok")) != 0) {
        websWrite(wp, T("Add User Cancelled"));
    } else if (gstrcmp(pass1, pass2) != 0) {
        websWrite(wp, T("Confirmation Password did not match."));
    } else {
        if (enabled && *enabled && (gstrcmp(enabled, T("on")) == 0)) {
            bDisable = FALSE;
        } else {
            bDisable = TRUE;
        }
        nCheck = umAddUser(userid, pass1, group, 0, bDisable);
        if (nCheck != 0) {
            char_t * strError;

            switch (nCheck) {
            case UM_ERR_DUPLICATE:
                strError = T("User already exists.");
                break;

            case UM_ERR_BAD_NAME:
                strError = T("Invalid user name.");
                break;

            case UM_ERR_BAD_PASSWORD:
                strError = T("Invalid password.");
                break;

            case UM_ERR_NOT_FOUND:
                strError = T("Invalid or unselected group.");
                break;

            default:
                strError = T("Error writing user record.");
                break;
            }

            websWrite(wp, T("Unable to add user, \"%s\".  %s"),
                userid, strError);
        } else {
            websWrite(wp, T("User, \"%s\" was successfully added."),
                userid);
        }
    }
    websWrite(wp, MSG_END);
    websFooter(wp);
    websDone(wp, 200);
}


static void formDeleteUser(webs_t wp, char_t *path, char_t *query)
{
    char_t  *userid, *ok;

    a_assert(wp);

    userid = websGetVar(wp, T("user"), T("")); 
    ok = websGetVar(wp, T("ok"), T("")); 

    websHeader(wp);
    websWrite(wp, MSG_START);

    if (gstricmp(ok, T("ok")) != 0) {
        websWrite(wp, T("Delete User Cancelled"));
    } else if (umUserExists(userid) == FALSE) {
        websWrite(wp, T("ERROR: User \"%s\" not found"), userid);
    } else if (umGetUserProtected(userid)) {
        websWrite(wp, T("ERROR: User, \"%s\" is delete-protected."), userid);
    } else if (umDeleteUser(userid) != 0) {
        websWrite(wp, T("ERROR: Unable to delete user, \"%s\" "), userid);
    } else {
        websWrite(wp, T("User, \"%s\" was successfully deleted."), userid);
    }
    websWrite(wp, MSG_END);
    websFooter(wp);
    websDone(wp, 200);
}


static void formDisplayUser(webs_t wp, char_t *path, char_t *query)
{
    char_t  *userid, *ok, *temp;
    bool_t  enabled;

    a_assert(wp);

    userid = websGetVar(wp, T("user"), T("")); 
    ok = websGetVar(wp, T("ok"), T("")); 

    websHeader(wp);
    websWrite(wp, T("<body>"));

    if (gstricmp(ok, T("ok")) != 0) {
        websWrite(wp, T("Display User Cancelled"));
    } else if (umUserExists(userid) == FALSE) {
        websWrite(wp, T("ERROR: User <b>%s</b> not found.\n"), userid);
    } else {
        websWrite(wp, T("<h2>User ID: <b>%s</b></h2>\n"), userid);
        temp = umGetUserGroup(userid);
        websWrite(wp, T("<h3>User Group: <b>%s</b></h3>\n"), temp);
        enabled = umGetUserEnabled(userid);
        websWrite(wp, T("<h3>Enabled: <b>%d</b></h3>\n"), enabled);
    }

    websWrite(wp, T("</body>\n"));
    websFooter(wp);
    websDone(wp, 200);
}


static int aspGenerateUserList(int eid, webs_t wp, int argc, char_t **argv)
{
    char_t  *userid;
    int     row, nBytesSent, nBytes;

    a_assert(wp);

    nBytes = websWrite(wp, 
        T("<SELECT NAME=\"user\" SIZE=\"3\" TITLE=\"Select a User\">"));
    row = 0;
    userid = umGetFirstUser();
    nBytesSent = 0;

    while (userid && (nBytes > 0)) {
        nBytes = websWrite(wp, T("<OPTION VALUE=\"%s\">%s\n"), userid, userid);
        userid = umGetNextUser(userid);
        nBytesSent += nBytes;
    }
    nBytesSent += websWrite(wp, T("</SELECT>"));
    return nBytesSent;
}


/*
    Add a group
 */
static void formAddGroup(webs_t wp, char_t *path, char_t *query)
{
    char_t          *group, *enabled, *privilege, *method, *ok, *pChar;
    int             nCheck;
    short           priv;
    accessMeth_t    am;
    bool_t          bDisable;

    a_assert(wp);

    group = websGetVar(wp, T("group"), T("")); 
    method = websGetVar(wp, T("method"), T("")); 
    enabled = websGetVar(wp, T("enabled"), T("")); 
    privilege = websGetVar(wp, T("privilege"), T("")); 
    ok = websGetVar(wp, T("ok"), T("")); 

    websHeader(wp);
    websWrite(wp, MSG_START);

    if (gstricmp(ok, T("ok")) != 0) {
        websWrite(wp, T("Add Group Cancelled."));
    } else if ((group == NULL) || (*group == 0)) {
        websWrite(wp, T("No Group Name was entered."));
    } else if (umGroupExists(group)) {
        websWrite(wp, T("ERROR: Group, \"%s\" already exists."), group);
    } else {
        if (privilege && *privilege) {
            /*
                privilege is a mulitple <SELECT> var, and must be parsed. Values for these variables are space delimited.
             */
            priv = 0;
            for (pChar = privilege; *pChar; pChar++) {
                if (*pChar == ' ') {
                    *pChar = '\0';
                    priv |= gatoi(privilege);
                    *pChar = ' ';
                    privilege = pChar + 1;
                }
            }
            priv |= gatoi(privilege);
        } else {
            priv = 0;
        }
        if (method && *method) {
            am = (accessMeth_t) gatoi(method);
        } else {
            am = AM_FULL;
        }
        if (enabled && *enabled && (gstrcmp(enabled, T("on")) == 0)) {
            bDisable = FALSE;
        } else {
            bDisable = TRUE;
        }
        nCheck = umAddGroup(group, priv, am, 0, bDisable);
        if (nCheck != 0) {
            websWrite(wp, T("Unable to add group, \"%s\", code: %d "),
                group, nCheck);
        } else {
            websWrite(wp, T("Group, \"%s\" was successfully added."), 
                group);
        }
    }
    websWrite(wp, MSG_END);
    websFooter(wp);
    websDone(wp, 200);
}


static void formDeleteGroup(webs_t wp, char_t *path, char_t *query)
{
    char_t  *group, *ok;

    a_assert(wp);

    group = websGetVar(wp, T("group"), T("")); 
    ok = websGetVar(wp, T("ok"), T("")); 

    websHeader(wp);
    websWrite(wp, MSG_START);

    if (gstricmp(ok, T("ok")) != 0) {
        websWrite(wp, T("Delete Group Cancelled."));
    } else if ((group == NULL) || (*group == '\0')) {
        websWrite(wp, T("ERROR: No group was selected."));
    } else if (umGetGroupProtected(group)) {
        websWrite(wp, T("ERROR: Group, \"%s\" is delete-protected."), group);
    } else if (umGetGroupInUse(group)) {
        websWrite(wp, T("ERROR: Group, \"%s\" is being used."), group);
    } else if (umDeleteGroup(group) != 0) {
        websWrite(wp, T("ERROR: Unable to delete group, \"%s\" "), group);
    } else {
        websWrite(wp, T("Group, \"%s\" was successfully deleted."), group);
    }
    websWrite(wp, MSG_END);
    websFooter(wp);
    websDone(wp, 200);
}


static int aspGenerateGroupList(int eid, webs_t wp, int argc, char_t **argv)
{
    char_t  *group;
    int     row, nBytesSent, nBytes;

    a_assert(wp);

    row = 0;
    nBytesSent = 0;
    nBytes = websWrite(wp, T("<SELECT NAME=\"group\" SIZE=\"3\" TITLE=\"Select a Group\">"));

    /*
     *  Add a special "<NONE>" element to allow de-selection
     */
    nBytes = websWrite(wp, T("<OPTION VALUE=\"\">[NONE]\n"));

    group = umGetFirstGroup();
    while (group && (nBytes > 0)) {
        nBytes = websWrite(wp, T("<OPTION VALUE=\"%s\">%s\n"), group, group);
        group = umGetNextGroup(group);
        nBytesSent += nBytes;
    }
    nBytesSent += websWrite(wp, T("</SELECT>"));
    return nBytesSent;
}


static void formAddAccessLimit(webs_t wp, char_t *path, char_t *query)
{
    char_t          *url, *method, *group, *secure, *ok;
    int             nCheck;
    accessMeth_t    am;
    short           nSecure;

    a_assert(wp);

    url = websGetVar(wp, T("url"), T("")); 
    group = websGetVar(wp, T("group"), T("")); 
    method = websGetVar(wp, T("method"), T("")); 
    secure = websGetVar(wp, T("secure"), T("")); 
    ok = websGetVar(wp, T("ok"), T("")); 

    websHeader(wp);
    websWrite(wp, MSG_START);

    if (gstricmp(ok, T("ok")) != 0) {
        websWrite(wp, T("Add Access Limit Cancelled."));
    } else if ((url == NULL) || (*url == 0)) {
        websWrite(wp, T("ERROR:  No URL was entered."));
    } else if (umAccessLimitExists(url)) {
        websWrite(wp, T("ERROR:  An Access Limit for [%s] already exists."), url);
    } else {
        if (method && *method) {
            am = (accessMeth_t) gatoi(method);
        } else {
            am = AM_FULL;
        }
        if (secure && *secure) {
            nSecure = (short) gatoi(secure);
        } else {
            nSecure = 0;
        }
        nCheck = umAddAccessLimit(url, am, nSecure, group);
        if (nCheck != 0) {
            websWrite(wp, T("Unable to add Access Limit for [%s]"), url);
        } else {
            websWrite(wp, T("Access limit for [%s], was successfully added."), url);
        }
    }
    websWrite(wp, MSG_END);
    websFooter(wp);
    websDone(wp, 200);
}


static void formDeleteAccessLimit(webs_t wp, char_t *path, char_t *query)
{
    char_t  *url, *ok;

    a_assert(wp);

    url = websGetVar(wp, T("url"), T("")); 
    ok = websGetVar(wp, T("ok"), T("")); 

    websHeader(wp);
    websWrite(wp, MSG_START);

    if (gstricmp(ok, T("ok")) != 0) {
        websWrite(wp, T("Delete Access Limit Cancelled"));
    } else if (umDeleteAccessLimit(url) != 0) {
        websWrite(wp, T("ERROR: Unable to delete Access Limit for [%s]"), url);
    } else {
        websWrite(wp, T("Access Limit for [%s], was successfully deleted."), url);
    }
    websWrite(wp, MSG_END);
    websFooter(wp);
    websDone(wp, 200);
}


static int aspGenerateAccessLimitList(int eid, webs_t wp, int argc, char_t **argv)
{
    char_t  *url;
    int     row, nBytesSent, nBytes;

    a_assert(wp);

    row = nBytesSent = 0;
    url = umGetFirstAccessLimit();
    nBytes = websWrite(wp, T("<SELECT NAME=\"url\" SIZE=\"3\" TITLE=\"Select a URL\">"));

    while (url && (nBytes > 0)) {
        nBytes = websWrite(wp, T("<OPTION VALUE=\"%s\">%s\n"), url, url);
        url = umGetNextAccessLimit(url);
        nBytesSent += nBytes;
    }
    nBytesSent += websWrite(wp, T("</SELECT>"));
    return nBytesSent;
}


static int aspGenerateAccessMethodList(int eid, webs_t wp, int argc, char_t **argv)
{
    int     nBytes;

    a_assert(wp);
    nBytes = websWrite(wp, T("<SELECT NAME=\"method\" SIZE=\"3\" TITLE=\"Select a Method\">"));
    nBytes += websWrite(wp, T("<OPTION VALUE=\"%d\">FULL ACCESS\n"), AM_FULL);
    nBytes += websWrite(wp, T("<OPTION VALUE=\"%d\">BASIC ACCESS\n"), AM_BASIC);
    nBytes += websWrite(wp, T("<OPTION VALUE=\"%d\" SELECTED>DIGEST ACCESS\n"), AM_DIGEST);
    nBytes += websWrite(wp, T("<OPTION VALUE=\"%d\">NO ACCESS\n"), AM_NONE);
    nBytes += websWrite(wp, T("</SELECT>")); 
    return nBytes;
}


static int aspGeneratePrivilegeList(int eid, webs_t wp, int argc, char_t **argv)
{
    int     nBytes;

    a_assert(wp);
    nBytes = websWrite(wp, T("<SELECT NAME=\"privilege\" SIZE=\"3\" "));
    nBytes += websWrite(wp, T("MULTIPLE TITLE=\"Choose Privileges\">"));
    nBytes += websWrite(wp, T("<OPTION VALUE=\"%d\">READ\n"), PRIV_READ);
    nBytes += websWrite(wp, T("<OPTION VALUE=\"%d\">EXECUTE\n"), PRIV_WRITE);
    nBytes += websWrite(wp, T("<OPTION VALUE=\"%d\">ADMINISTRATE\n"), PRIV_ADMIN);
    nBytes += websWrite(wp, T("</SELECT>"));
    return nBytes;
}


static void formSaveUserManagement(webs_t wp, char_t *path, char_t *query)
{
    char_t  *ok;

    a_assert(wp);

    ok = websGetVar(wp, T("ok"), T("")); 
    websHeader(wp);
    websWrite(wp, MSG_START);

    if (gstricmp(ok, T("ok")) != 0) {
        websWrite(wp, T("Save Cancelled."));
    } else if (umCommit(NULL) != 0) {
        websWrite(wp, T("ERROR: Unable to save user configuration."));
    } else {
        websWrite(wp, T("User configuration was saved successfully."));
    }
    websWrite(wp, MSG_END);
    websFooter(wp);
    websDone(wp, 200);
}


static void formLoadUserManagement(webs_t wp, char_t *path, char_t *query)
{
    char_t  *ok;

    a_assert(wp);

    ok = websGetVar(wp, T("ok"), T("")); 
    websHeader(wp);
    websWrite(wp, MSG_START);
    if (gstricmp(ok, T("ok")) != 0) {
        websWrite(wp, T("Load Cancelled."));
    } else if (umRestore(NULL) != 0) {
        websWrite(wp, T("ERROR: Unable to load user configuration."));
    } else {
        websWrite(wp, T("User configuration was re-loaded successfully."));
    }
    websWrite(wp, MSG_END);
    websFooter(wp);
    websDone(wp, 200);
}

#endif /* BIT_USER_MANAGEMENT */

/*
    @copy   default

    Copyright (c) Embedthis Software LLC, 2003-2012. All Rights Reserved.

    This software is distributed under commercial and open source licenses.
    You may use the Embedthis GoAhead open source license or you may acquire 
    a commercial license from Embedthis Software. You agree to be fully bound
    by the terms of either license. Consult the LICENSE.md distributed with
    this software for full details and other copyrights.

    Local variables:
    tab-width: 4
    c-basic-offset: 4
    End:
    vim: sw=4 ts=4 expandtab

    @end
 */