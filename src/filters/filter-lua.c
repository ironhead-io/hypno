/* ------------------------------------------- * 
 * filter-lua.c 
 * =============
 * 
 * Summary 
 * -------
 * - 
 *
 * LICENSE
 * -------
 * Copyright 2020-2021 Tubular Modular Inc. dba Collins Design
 *
 * See LICENSE in the top-level directory for more information.
 *
 * CHANGELOG 
 * ---------
 * -
 * ------------------------------------------- */
#include "filter-lua.h"

#define DYLIB ".so"

static const char rname[] = "route";

static const char def[] = "default";

static const char confname[] = "config.lua";

static const char configkey[] = "config";

static const char modelkey[] = "model";

static const char rkey[] = "routes";

static const char ctype_def[] = "text/html";

static const char extfmt[] = "%s;%s/?;%s/?.lua";

static const char libcfmt[] = "%s;%s/lib/?" DYLIB;

typedef enum {
	CTYPE_TEXTHTML
,	CTYPE_PLAINTEXT
, CTYPE_JSON
, CTYPE_XML
} ctypen_t;


typedef struct ctype_t { 
	const char *ctypename; 
	ctypen_t ctype; 
} ctype_t;


ctype_t ctypes_serializable[] = {
	{ ctype_def, CTYPE_TEXTHTML }
,	{ "text/plain", CTYPE_PLAINTEXT }
,	{ "application/json", CTYPE_JSON }
, { "application/xml", CTYPE_XML }
, { "text/xml", CTYPE_XML }
, { NULL }
};

static const char *ctype_tags[] = {
	"ctype"
, "content-type"
, "contenttype" 
, NULL
};


//HTTP error
static int http_error( zhttp_t *res, int status, char *fmt, ... ) { 
	va_list ap;
	char err[ 2048 ];
	memset( err, 0, sizeof( err ) );
	va_start( ap, fmt );
	vsnprintf( err, sizeof( err ), fmt, ap );
	va_end( ap );
	memset( res, 0, sizeof( zhttp_t ) );
	res->clen = strlen( err );
	http_set_status( res, status ); 
	http_set_ctype( res, ctype_def );
	http_copy_content( res, err , strlen( err ) );
	zhttp_t *x = http_finalize_response( res, err, strlen( err ) ); 
	return x ? 1 : 0;
}


#if 0
//TODO: This is an incredibly difficult way to make things 
//read-only...  Is there a better one?
static const char read_only_block[] = " \
	function make_read_only( t ) \
		local tt = {} \
		local mt = { \
			__index = t	 \
		,	__newindex = function (t,k,v) error( 'something went wrong', 1 ) end \
		}	\
		setmetatable( tt, mt ) \
		return tt \
	end \
";
#endif



//TODO: Change me to accept a function pointer...
static struct mvcmeta_t { 
	const char *dir; 
	const char *ext;
	const char *reserved; 
	int (*fp)( int );
} mvcmeta [] = {
	{ "app", "lua", "model,models" }
,	{ "sql", "sql", "query,queries" }
,	{ "views", "tpl", "view,views" } 
,	{ NULL, NULL, "content-type" }
//,	{ NULL, "inherit", NULL }
};



//....
int run_lua_buffer( lua_State *L, const char *buffer ) {
	//Load a buffer	
	luaL_loadbuffer( L, buffer, strlen( buffer ), "make-read-only-function" );
	if ( lua_pcall( L, 0, LUA_MULTRET, 0 ) != LUA_OK ) {
		fprintf( stdout, "lua string exec failed: %s", (char *)lua_tostring( L, -1 ) );
		//This shouldn't fail, but if it does you should stop...
		return 0;
	}
	return 1;
}


#if 0
//In lieu of a C-only way to make members read only, use this
static int make_read_only ( lua_State *L, const char *table ) {
	//Certain tables (and their children) need to be read-only
	int err = 0;
	const char fmt[] = "%s = make_read_only( %s )";
	char execbuf[ 256 ] = { 0 };
	snprintf( execbuf, sizeof( execbuf ), fmt, table, table );

	//Load a buffer	
	luaL_loadbuffer( L, execbuf, strlen( execbuf ), "make-read-only" );
	if ( ( err = lua_pcall( L, 0, LUA_MULTRET, 0 ) ) != LUA_OK ) {
		fprintf( stdout, "lua string exec failed: %s", (char *)lua_tostring( L, -1 ) );
		//This shouldn't fail, but if it does you should stop...
		return 0;
	}
	return 1;
}
#endif



//Should return an error b/c there are some situations where this does not work.
int lua_loadlibs( lua_State *L, struct lua_fset *set ) {
	//Now load everything written elsewhere...
	for ( ; set->namespace; set++ ) {
		lua_newtable( L );
		for ( struct luaL_Reg *f = set->functions; f->name; f++ ) {
			lua_pushstring( L, f->name );
			lua_pushcfunction( L, f->func );	
			lua_settable( L, 1 );
		}
		lua_setglobal( L, set->namespace );
	}

#if 0
	//And finally, add some functions that we'll need later (if this fails, meh)
	if ( !run_lua_buffer( L, read_only_block ) ) {
		return 0;
	}
#endif
	return 1;
}



//Check if there is a reserved keyword being requested
static int is_reserved( const char *a ) {
	for ( int i = 0; i < sizeof( mvcmeta ) / sizeof( struct mvcmeta_t ); i ++ ) {
#if 0
		if ( memstrat( mvcmeta[i].reserved, a, strlen( mvcmeta[i].reserved ) ) > -1 ) {
			return 1;
		}
#else
		zWalker w = {0};
		for ( ; strwalk( &w, mvcmeta[i].reserved, "," ); ) {
	//int sl = strlen( (char *)mvcmeta[ i ].reserved );
		//for ( ; memwalk( &w, (unsigned char *)mvcmeta[i].reserved, (unsigned char *)",", sl, 1 ); ) {
#if 0
fprintf( stderr, 
	"POS: %d Size: %d Len: %ld Next: %d\n",
	w.pos, w.size, strlen(mvcmeta[i].reserved), w.next ); getchar();
#endif
			char buf[64];
			memset( buf, 0, sizeof( buf ) );
			memcpy( buf, w.src, ( w.chr == ',' ) ? w.size - 1 : w.size );
			if ( strcmp( a, buf ) == 0 ) return 1;
		}
#endif
	}
	return 0;
}



//Make a route list
static int make_route_list ( zKeyval *kv, int i, void *p ) {
	struct route_t *tt = (struct route_t *)p;
	const int routes_wordlen = 6;
	if ( kv->key.type == ZTABLE_TXT && !is_reserved( kv->key.v.vchar ) ) {
		char key[ 2048 ] = { 0 };
		lt_get_full_key( tt->src, i, (unsigned char *)&key, sizeof( key ) );
		//replace all '.' with '/'
		for ( char *k = key; *k; k++ ) ( *k == '.' ) ? *k = '/' : 0;	
		struct iroute_t *ii = malloc( sizeof( struct iroute_t ) );
		ii->index = i, ii->route = zhttp_dupstr( &key[ routes_wordlen ] ), *ii->route = '/';
		add_item( &tt->iroute_tlist, ii, struct iroute_t *, &tt->iroute_tlen );
	}
	return 1;	
}



//Create a list of resources (an alternate version of this will inherit everything) 
static int make_mvc_list ( zKeyval *kv, int i, void *p ) {
	struct mvc_t *tt = (struct mvc_t *)p;
	char *key = NULL;
	int ctype = 0;

	if ( tt->depth == 1 ) {
		if ( kv->key.type == ZTABLE_TXT && is_reserved( key = kv->key.v.vchar ) ) {
			if ( !strcmp( key, "model" ) || !strcmp( key, "models" ) )
				tt->mset = &mvcmeta[ 0 ], tt->type = kv->value.type, tt->model = 1;
			else if ( !strcmp( key, "query" ) )
				tt->mset = &mvcmeta[ 1 ], tt->type = kv->value.type, tt->query = 1;
			else if ( !strcmp( key, "content-type" ) )
				tt->mset = &mvcmeta[ 3 ], tt->type = kv->value.type, ctype = 1;
			else if ( !strcmp( key, "view" ) || !strcmp( key, "views" ) ) {
				tt->mset = &mvcmeta[ 2 ], tt->type = kv->value.type, tt->view = 1;
			}
		}

		//write content type
		if ( tt->mset && ctype ) {
			memcpy( (char *)tt->ctype, kv->value.v.vchar, strlen( kv->value.v.vchar ) );
			return 1;
		}
	}

	if ( kv->value.type == ZTABLE_TBL ) {
		tt->depth++;
		return 1;
	}

	if ( tt->mset && kv->value.type == ZTABLE_TXT && memchr( "mvq", *tt->mset->reserved, 3 ) ) {
		struct imvc_t *imvc = malloc( sizeof( struct imvc_t ) );
		memset( imvc, 0, sizeof( struct imvc_t ) );
		snprintf( (char *)imvc->file, sizeof(imvc->file) - 1, "%s/%s.%s", 
			tt->mset->dir, kv->value.v.vchar, tt->mset->ext );
		snprintf( (char *)imvc->base, sizeof(imvc->base) - 1, "%s.%s", 
			kv->value.v.vchar, tt->mset->ext );
		snprintf( (char *)imvc->ext, sizeof(imvc->ext) - 1, "%s", 
			tt->mset->ext );
		add_item( &tt->imvc_tlist, imvc, struct imvc_t *, &tt->flen );
	}

	if ( kv->key.type == ZTABLE_TRM || tt->type == ZTABLE_TXT ) {
		tt->mset = NULL;	
	}
	if ( kv->key.type == ZTABLE_TRM ) {
		tt->depth--;
	}
	return 1;	
}



//Free MVC list
static void free_mvc_list ( void ***list ) {
	for ( void **l = *list; l && *l; l++ ) {
		free( *l );
	} 
	free( *list ), *list = NULL;
}



//Free route list
static void free_route_list ( struct iroute_t **list ) {
	for ( struct iroute_t **l = list; *l; l++ ) {
		free( (*l)->route ), free( *l );
	}
	free( list );
}



//Return NULL if there are no files
static struct dirent * dir_has_files ( DIR *dir ) {
	int fcount = 0;
	struct dirent *d = NULL;
	for ( int dlen ; ( d = readdir( dir ) ); ) {
		if ( ( dlen = strlen( d->d_name ) > 4 ) && strstr( d->d_name, ".lua" ) ) {
			//TODO: Will this need to happen b/c it's a different scope?
			rewinddir( dir );
			return d; 
		}
	}
	return d;
}



//Load Lua configuration
static int load_lua_config( struct luadata_t *l ) {
	char *db, *fqdn, cpath[ 2048 ] = { 0 };
	DIR *dir = NULL;
	struct dirent *d = NULL;
	struct stat sb = { 0 };
	ztable_t *t = NULL;
	int count = 0;

	//Create a full path to the config file
	snprintf( cpath, sizeof(cpath) - 1, "%s/%s", l->root, confname );

	//Open the configuration file
	if ( !lua_exec_file( l->state, cpath, l->err, LD_ERRBUF_LEN ) ) {
		//snprintf( l->err, LD_ERRBUF_LEN, "Execution of %s/%s failed.", l->root, "config.lua" );
		return 0;
	}

	//If it's anything but a Lua table, we're in trouble
	if ( !lua_istable( l->state, 1 ) ) {
		snprintf( l->err, LD_ERRBUF_LEN, "Configuration is not a Lua table." );
		return 0;
	}

	//Set shadow path
	lua_pushstring( l->state, l->root );
	lua_setglobal( l->state, "shadow" );

	//Check if there are any route files
	snprintf( cpath, sizeof(cpath) - 1, "%s/%s", l->root, rkey );

	//Get a directory listing
	if ( !( dir = opendir( cpath ) ) || !dir_has_files( dir ) ) {
		count = lua_count( l->state, 1 );
	}
	else {
		//Find the routes table and put that on the stack
		lua_pushnil( l->state );
		for ( ; lua_next( l->state, 1 ); ) {
			if ( lua_type( l->state, -2 ) == LUA_TSTRING && !strcmp( lua_tostring( l->state, -2 ), "routes" ) ) {
				lua_remove( l->state, -2 );
				break;
			}
			lua_pop( l->state, 1 );
		}

		//Load each route file and combine it with the route table
		for ( int dlen, ii; ( d = readdir( dir ) ) ; ) {
			//Only deal with regular Lua files (eventually can support symbolic links)
			FPRINTF( "Checking for valid route file at: %s/%s\n", cpath, d->d_name );
			if ( ( dlen = strlen( d->d_name ) > 4 ) && strstr( d->d_name, ".lua" ) && d->d_type == DT_REG ) { 
				snprintf( cpath, sizeof(cpath) - 1, "%s/%s/%s", l->root, "routes", d->d_name );

				//Open each file in the directory?
				if ( !lua_exec_file( l->state, cpath, l->err, LD_ERRBUF_LEN ) ) {
					//snprintf( stderr, "Lua error: %s\n", l->err );
					//snprintf( l->err, LD_ERRBUF_LEN, "Failed to run file at %s did not return a table.", cpath );
					return 0;
				}

				//The resultant value should ALWAYS be a table
				if ( !lua_istable( l->state, ( ii = lua_gettop( l->state ) ) ) ) {
					snprintf( l->err, LD_ERRBUF_LEN, "File at %s did not return a table.", cpath );
					return 0;
				}

				//Loop through all the top-level keys
				lua_pushnil( l->state );
				for ( const char *vv; lua_next( l->state, ii ); ) {
					if ( lua_type( l->state, -2 ) != LUA_TSTRING ) {
						snprintf( l->err, LD_ERRBUF_LEN, "Key in table at %s was not a string.", cpath );
						return 0;
					}

					if ( lua_type( l->state, -1 ) != LUA_TTABLE ) {
						snprintf( l->err, LD_ERRBUF_LEN, "Value at %s in table at %s was not a string.", cpath, lua_tostring( l->state, -2 ) );
						return 0;
					}

					//Push the key back for iteration's sake
					vv = lua_tostring( l->state, -2 );
					lua_settable( l->state, 2 );
					lua_pushstring( l->state, vv );
				}
			}
		}

		//Clean up the stack and free the original set of routes
		lua_pop( l->state, 1 );

		//Add key and set table
		lua_pushstring( l->state, "routes" );
		lua_insert( l->state, 2 );
		lua_settable( l->state, 1 );

		//Close the directory
		count = lua_count( l->state, 1 );
	}

	//Close the directory
	closedir( dir );

	//If there is a zero count for whatever reason, this needs to stop
	if ( !count ) {
		snprintf( l->err, LD_ERRBUF_LEN, "Configuration table has no values." );
		return 0;
	}

	//Initialize a table of the right size
	if ( !( t = lt_make( count * 2 ) ) || !lua_to_ztable( l->state, 1, t ) ) {
		snprintf( l->err, LD_ERRBUF_LEN, "Conversion of config to ztable failed." );
		return 0;
	}

	//Lock?
	lt_lock( l->zconfig = t );

	//TODO: use pointers instead.  There is no reason to copy all of this...
	if ( ( db = lt_text( t, "db" ) ) ) {
		memcpy( (void *)l->db, db, strlen( db ) ); 
	}

	if ( ( fqdn = lt_text( t, "fqdn" ) ) ) {
		memcpy( (void *)l->fqdn, fqdn, strlen( fqdn ) ); 
	}

	lua_pop( l->state, 1 );
	return 1;
}




//Checking for static paths is important, also need to check for disallowed paths
static int path_is_static ( struct luadata_t *l ) {
	int i, size, ulen = strlen( l->req->path );
	if ( ( i = lt_geti( l->zconfig, "static" ) ) == -1 ) {
		return 0;
	}
	
	//Start at the pulled index, count x times, and reset?
	for ( int len, ii = 1, size = lt_counta( l->zconfig, i ); ii < size; ii++ ) {
		zKeyval *kv = lt_retkv( l->zconfig, i + ii );
		//pbuf[ ii - 1 ] = kv->value.v.vchar;
		len = strlen( kv->value.v.vchar );

		//I think I can just calculate the current path
		if ( len <= ulen && memcmp( kv->value.v.vchar, l->req->path, len ) == 0 ) {
			return 1;
		}
	}

	return 0;
}



//Send a static file
static const int send_static ( zhttp_t *res, const char *dir, const char *uri ) {
	//Read_file and return that...
	struct stat sb;
	int fd = 0;
	char err[ 2048 ] = { 0 }, spath[ 2048 ] = { 0 };
	unsigned char *data;
	const struct mime_t *mime;
	memset( spath, 0, sizeof( spath ) );
	snprintf( spath, sizeof( spath ) - 1, "%s/%s", dir, ++uri );

	//Check if the path is there at all (read-file can't do this)
	if ( stat( spath, &sb ) == -1 ) {
		return http_error( res, 404, "File '%s' not found", spath );
	}

	//Get its mimetype
	if ( !( mime = zmime_get_by_filename( spath ) ) ) {
		mime = zmime_get_default();
	}
#if 0
	//write max should be checked.
	//...
	int dlen = 0;

	//read_file and send back
	if ( !( data = read_file( spath, &dlen, err, sizeof( err ) ) ) ) {
		return http_error( res, 500, "static read failed: %s", err );
	}

	//Gotta get the mimetype too	
	if ( !( mime = zmime_get_by_filename( spath ) ) ) {
		mime = zmime_get_default();
	}

	//Send the message out
	res->clen = dlen;
	http_set_status( res, 200 ); 
	http_set_ctype( res, mime->mimetype );
	http_set_content( res, data, dlen );
	if ( !http_finalize_response( res, err, sizeof(err) ) ) {
		return http_error( res, 500, err );
	}

	free( data );
#else
	//Open the file
	if ( ( fd = open( spath, O_RDONLY ) ) == -1 ) {
		return http_error( res, 404, strerror( errno ) );
	}

	//Prepare the message
	#if 1
	//TODO: Enable non sendfile capable systems to be able to send a file the old crappy way.
	res->atype = ZHTTP_MESSAGE_SENDFILE;
	res->clen = sb.st_size;
	res->fd = fd;
	res->status = 200;
	res->ctype = (char *)mime->mimetype;
	#else
	http_set_fd( res, fd );
	http_set_content_length( res, sb.st_size );
	http_set_message_type( res, ZHTTP_MESSAGE_SENDFILE );
	http_set_status( res, 200 );
	http_set_ctype( res, mime->mimetype );
	#endif

	if ( !http_finalize_response( res, err, sizeof(err) ) ) {
		return http_error( res, 500, err );
	}
#endif
	return 1;
}


//...
static void dump_records( struct HTTPRecord **r ) {
	int b = 0;
	for ( struct HTTPRecord **a = r; a && *a; a++ ) {
		fprintf( stderr, "%p: %s -> ", *a, (*a)->field ); 
		b = write( 2, (*a)->value, (*a)->size );
		b = write( 2, "\n", 1 );
	}
}


static char * getpath( char *rp, char *ap, int destlen ) {
	int pos = 0, len = strlen( rp );

	if ( ( pos = memchrat( rp , '?', len ) ) > -1 ) {
		len = pos;
	}

	if ( destlen <= pos ) {
		return NULL;
	}
	
	for ( char *p = ap, *path = rp; *path && *path != '?'; ) {
		*(p++) = *(path++);
	}

	return ap;
}


//Initialize routes in Lua
static int init_lua_routes ( struct luadata_t *l ) {
	zWalker w = {0}, w2 = {0};
	const char *active = NULL, *path = l->apath + 1, *resolved = l->rroute + 1;
	char **routes = { NULL };
	int index = 0, rlen = 0, pos = 1;
	
	//Add a table.
	lua_newtable( l->state );

	//Handle root requests
	if ( !*path ) {
		lua_pushinteger( l->state, 1 ); 
		lua_pushstring( l->state, def ); 
		lua_settable( l->state, pos );

		lua_pushstring( l->state, "active" ); 
		lua_pushstring( l->state, def ); 
		lua_settable( l->state, pos );

		memcpy( (void *)l->aroute, def, strlen (def) );
		return 1;
	} 
	
	// Loop twice to set up the map
	for ( ; strwalk( &w, path, "/" ); ) {
		// ...
		char stub[ 1024 ] = {0};
		char id[ 1024 ] = {0};

		// Write the length of the block between '/'
		memset( stub, 0, sizeof(stub) );
		memcpy( stub, w.src, ( w.chr == '/' ) ? w.size - 1 : w.size );

		for ( ; strwalk( &w2, resolved, "/" ); ) {
			int size = ( w2.chr == '/' ) ? w2.size - 1 : w2.size;
			// If there is an equal, most likely it's an id
			if ( *w2.src != ':' )
				lua_pushinteger( l->state, ++index );	
			else {
				// Find the key/id name
				for ( char *p = (char *)w2.src, *b = id; *p && ( *p	!= '=' || *p != '/' ); ) {
					*(b++) = *(p++);
				}

				// Check that id is not active, because that's a built-in
				if ( strcmp( id, "active" ) == 0 ) {
					return 0;
				}
				
				// Add a numeric key first, then a text key
				lua_pushinteger( l->state, ++index );
				lua_pushstring( l->state, stub );
				lua_settable( l->state, pos );
				lua_pushstring( l->state, id );
			}
			break;
		}

		//Copy the value (stub) to value in table
		lua_pushstring( l->state, stub );
		lua_settable( l->state, pos );
		active = &path[ w.pos ];
	}

	lua_pushstring( l->state, "active" );
	lua_pushstring( l->state, active );
	lua_settable( l->state, pos );
	memcpy( (void *)l->aroute, active, strlen (active) );
	return 1;
}



//Initialize HTTP in Lua
static int init_lua_request ( struct luadata_t *l ) {
	//Loop through all things
	const char *str[] = { "headers", "url" };
	struct HTTPRecord **ii[] = { l->req->headers, l->req->url, l->req->body };

	//Add one table for all structures
	lua_newtable( l->state );

	//Add general request info
	lua_setstrstr( l->state, "path", l->req->path, 1 );
	lua_setstrstr( l->state, "method", l->req->method, 1 );
	lua_setstrstr( l->state, "protocol", l->req->protocol, 1 );
	lua_setstrstr( l->state, "host", l->req->host, 1 );

	//In some cases browsers omit the content-type
	if ( !strcmp( l->req->method, "GET" ) && !strcmp( l->req->ctype, "application/octet-stream" ) ) 
		lua_setstrstr( l->state, "ctype", "text/html", 1 );
	else {
		lua_setstrstr( l->state, "ctype", l->req->ctype, 1 );
	}

	//If the method is NOT idempotent, don't bother with content-length
	if ( l->req->idempotent )
		lua_setstrint( l->state, "clength", l->req->clen, 1 );

	//Add simple keys for headers and URL
	for ( int pos = 3, i = 0; i < 2; i++ ) {
		struct HTTPRecord **r = ii[ i ];
		if ( r && *r ) {
			lua_pushstring( l->state, str[i] ), lua_newtable( l->state );
			for ( ; r && *r; r++ ) {
			#if 0	
				if ( strcmp( "Cookie", (*r)->field ) != 0 )
					lua_pushstring( l->state, (*r)->field );
				else {	
					char *f = (char *)(*r)->field; 
					*f = ( *f > 63 && *f < 91 ) ? *f + 32 : *f;
					lua_pushstring( l->state, f );
				}
			#else
				char *f = (char *)(*r)->field; 
				*f = ( *f > 63 && *f < 91 ) ? *f + 32 : *f;
				lua_pushstring( l->state, f );
			#endif

				//Add the lower case version of whatever the header title may be
				lua_newtable( l->state );
				lua_setstrbin( l->state, "value", ( char * )(*r)->value, (*r)->size, pos + 2 );
				lua_setstrint( l->state, "size", (*r)->size, pos + 2 );

			#if 0 
				//For now, we only need to worry with authentication and cookies
				if ( strcmp( "cookie", (*r)->field ) == 0 ) {

					zw_t ww, *w = memset( &ww, 0, sizeof( zw_t ) );
					//int count = 0;
					for ( int x = 0, p = 0; memwalk( w, (*r)->value, (unsigned char *)"=;", (*r)->size, 2 ); ) {

						//Get size and initialize a buffer
						unsigned char *b, buf[ 256 ] = {0};
						int si = 0, size = memchr( "=;", w->chr, 2 ) ? w->size - 1 : w->size;

						//Die on sizes that are too large 
						if ( size >= 256 ) {
							snprintf( l->err, LD_ERRBUF_LEN, "Header %s too large.", i ? "value" : "key" );
							return 0;
						}

						//Trim any excess from current value
						b = trim( w->src, "\r\"' \t", size, &si );	
						if ( si > 0 ) {
							memcpy( buf, b, si );
							if ( w->chr == '=' )
								lua_pushstring( l->state, (char *)buf ), p++;
							else if ( !p && w->chr == ';' )	{
								lua_pushnumber( l->state, x++ ), lua_pushstring( l->state, (char *)buf );
								lua_settable( l->state, pos + 2 );
							}
							else {
								lua_pushstring( l->state, (char *)buf );
								lua_settable( l->state, pos + 2 );
								p = 0;
							}
						}
					}
				}
			#endif
				lua_settable( l->state, pos );
			}
			lua_settable( l->state, 1 );
		}
	}

	//We gotta do the body now	
	struct HTTPRecord **b; 
	if ( ( b = l->req->body ) ) {
		lua_pushstring( l->state, "body" ), lua_newtable( l->state );
		if ( l->req->formtype == ZHTTP_OTHER ) {
			lua_setstrbin( l->state, "value", (*b)->value, (*b)->size, 3 );
			lua_setstrint( l->state, "size", (*b)->size, 3 );
		}
		else {
			for ( ; b && *b; b++ ) {
				lua_pushstring( l->state, (*b)->field );
				lua_newtable( l->state );
				lua_setstrbin( l->state, "value", (*b)->value, (*b)->size, 5 );
				lua_setstrint( l->state, "size", (*b)->size, 5 );
			#if 0
				//Content-disposition
				//Filename?
				//Any other fields?
			#endif
				lua_settable( l->state, 3 );
			} 
		}
		lua_settable( l->state, 1 );			
	}	

	//Set global name
	return 1;
}


static int init_lua_shadowpath ( struct luadata_t *l ) {
	lua_pushstring( l->state, l->root );
	return 1;	
}


static int init_lua_config ( struct luadata_t *l ) {
	return ztable_to_lua( l->state, l->zconfig ); 
}


//Data to initialize global elements
static struct lua_readonly_t {
	const char *name;
	int (*exec)( struct luadata_t * );
	//int (*exec)( lua_State *, zhttp_t *, const char *, const char * );	
} lua_readonly[] = {
  { "config", init_lua_config }
, { "request", init_lua_request }
, { "route", init_lua_routes }
#if 0
, { "shadow", init_lua_shadowpath }
, { "cache", init_lua_cache }
#endif
, { NULL }
};


static int free_ld ( struct luadata_t *l ) {
	lua_close( l->state );
	lt_free( l->zconfig ), free( l->zconfig );
	lt_free( l->zroute ), free( l->zroute );
	lt_free( l->zmodel ), free( l->zmodel );
	free_mvc_list( (void ***)&(l->pp.imvc_tlist) );
	return 1;
}


char * text_encode ( ztable_t *t ) {
	char *c = malloc( 1 );
	*c = '\0';
	return c;
}


static zhttp_t * return_as_serializable ( struct luadata_t *l, ctype_t *t ) {
	char * content = NULL; 
	const char *ctype = NULL;
	int clen = 0;
	zhttp_t *p = NULL;
	
	if ( 0 ) { ; }
	#if 0
	else if ( t->ctype == CTYPE_XML ) {
		content = xml_encode( l->zmodel, "model" );
		clen = strlen( content );
		ctype = t->ctypename;
	}
	#endif
	else if ( t->ctype == CTYPE_JSON ) {
	#if 0
		content = zjson_encode( l->zmodel, l->err, 1024 );
	#else
		struct mjson **zjson = NULL;
		if ( !( zjson = ztable_to_zjson( l->zmodel, l->err, 1024 ) ) ) {
			return NULL;
		}
		if ( !( content = zjson_stringify( zjson, l->err, 1024 ) ) ) {
			zjson_free( zjson );
			return NULL;
		}
		zjson_free( zjson );
	#endif
		clen = strlen( content );
		ctype = t->ctypename;
	}
	else {
		//TODO: This should handle the other types... 
		content = text_encode( l->zmodel );
		clen = strlen( content );
		ctype = "text/plain";
	}

	l->res->clen = clen;
	http_set_status( l->res, 200 ); 
	http_set_ctype( l->res, t->ctypename );
	http_set_content( l->res, (unsigned char *)content, l->res->clen ); 

	//Return the finished message if we got this far
	p = http_finalize_response( l->res, l->err, LD_ERRBUF_LEN );

	free( content );
	return p;
}




//Check if the user asked to delay the response...
static int delay_response ( struct luadata_t *l ) {
	return 1;
}



//...
static int return_as_response ( struct luadata_t *l ) {

	ztable_t *rt = NULL;
	int count = 0, status = 200, clen = 0;
	int header_i = 0;
	int status_i = 0;
	int ctype_i = 0;
	int clen_i = 0;
	int file_i = 0;
	int prepped_own_content = 0;
	int content_i = 0;
	int delayed = 0;
	char ctype[ 128 ] = { 0 }; //'t','e','x','t','/','h','t','m','l','\0', 0 };
	unsigned char *content = NULL;

#if 1
	count = 512;
#else
	//Get the count to approximate size of conversion needed (and to handle blanks)
	count = lua_count( l->state, 1 );
#endif
	
	if ( !lua_istable( l->state, 1 ) ) {
		snprintf( l->err, LD_ERRBUF_LEN, "Response is not a table." );
		return 0;
	}

	if ( !( rt = lt_make( count * 2 ) ) ) {
		lt_free( rt ), free( rt );
		snprintf( l->err, LD_ERRBUF_LEN, "Could not generate response table." );
		return 0;
	}

	if ( !lua_to_ztable( l->state, 1, rt ) ) {
		lt_free( rt ), free( rt );
		snprintf( l->err, LD_ERRBUF_LEN, "Error in model conversion." );
		return 0;
	}

	//Lock the ztable to enable proper hashing and collision mgmt
	if ( !lt_lock( rt ) ) {
		snprintf( l->err, LD_ERRBUF_LEN, "%s", lt_strerror( rt ) );
		return 0;
	}

	//Check if the user wants to delay the response.
	if ( lt_geti( rt, "delay" ) > -1 ) {
		delayed = 1;
	}

	//Get the status
	if ( ( status_i = lt_geti( rt, "status" ) ) > -1 ) {
		status = lt_int_at( rt, status_i );
	}
	
	//Get the content-type (if there is one)
	if ( ( ctype_i = lt_geti( rt, "ctype" ) ) > -1 ) {
		snprintf( ctype, sizeof( ctype ) - 1, "%s", lt_text_at( rt, ctype_i ) ); 
	}

	//Get the content-length (if there is one)
	if ( ( clen_i = lt_geti( rt, "clen" ) ) > -1 ) {
		clen = lt_int_at( rt, clen_i ); 
	}

	//Get the content
	if ( !delayed && ( content_i = lt_geti( rt, "content" ) ) > -1 ) {
		content = (unsigned char *)lt_text_at( rt, content_i );
		if ( clen_i == -1 ) {
			if ( !content ) {
				// TODO: A stack trace showing where exactly the lack of content came from would be very helpful
				prepped_own_content = 1;
				char *c = malloc( 256 );
				memset( c, 0, 256 );
				snprintf( c, 255, "%d %s - No content specified\n", status, http_get_status_text( status ) );
				content = (unsigned char *)c;
			}
			clen = strlen( (char *)content );
		}
	}

	//In this case, set clen with the file
	if ( !delayed && ( file_i = lt_geti( rt, "file" ) ) > -1 ) {
		const char * fname = lt_text_at( rt, file_i );
		char fbuf[ PATH_MAX ];
		int len = 0;
		memset( fbuf, 0, PATH_MAX );

		//Do I need a shadow?
		snprintf( fbuf, sizeof( fbuf ) - 1, "%s/%s", l->root, fname );

		//Problem reading the file
		if ( !( content = read_file( fbuf, &len, l->err, LD_ERRBUF_LEN ) ) )  {
			lt_free( rt ), free( rt );
			return 0;
		}

		//const struct mime_t *mime = zmime_get_by_filename( fbuf );
		snprintf( ctype, sizeof( ctype ) - 1, "%s", zmime_get_mimetype( zmime_get_by_filename( fbuf ) ) );
		clen = len;
	}

	//Set content type to default if it was not set anywhere else
	if ( *ctype == 0 ) {
		snprintf( ctype, sizeof( ctype ) - 1, "%s", ctype_def );
	}

	//Likewise, if all we have is status, and no file or content then
	//you need to error with a 500
	if ( status_i > -1 && file_i == -1 && content_i == -1 ) {
		lt_free( rt ), free( rt );
		snprintf( l->err, LD_ERRBUF_LEN, "Status specified with no content." );
		return 0;
	}

	//Finally, get any headers if there are any
	if ( ( header_i = lt_geti( rt, "headers" ) ) > -1 ) {
		//You can use get keys or loop through the thing...
		for ( zKeyval *kv = lt_items( rt, "headers" ); ( kv = lt_items( rt, "headers" ) ); ) {
			if ( kv->key.type == ZTABLE_TRM )
				break;
			if ( kv->key.type	!= ZTABLE_TXT && (  kv->value.type	!= ZTABLE_TXT && kv->value.type != ZTABLE_INT ) ) { 
				snprintf( l->err, LD_ERRBUF_LEN, "Got invalid header value." );
				return 0; // die
			}
 
			if ( kv->value.type == ZTABLE_TXT )
				http_set_header( l->res, kv->key.v.vchar, kv->value.v.vchar );
			else if ( kv->value.type == ZTABLE_INT ) {
				char intbuf[ 64 ] = {0};
				snprintf( intbuf, sizeof( intbuf ), "%d", kv->value.v.vint );
				http_set_header( l->res, kv->key.v.vchar, intbuf );
			}
		}
	}

	if ( !delayed ) {
		//Set structures
		l->res->clen = clen;
		http_set_status( l->res, status ); 
		http_set_ctype( l->res, ctype );
		http_set_content( l->res, content, clen ); 

		//Return finalized content
		zhttp_t *rr = http_finalize_response( l->res, l->err, LD_ERRBUF_LEN ); 
		lt_free( rt ), free( rt );
		if ( file_i > -1 || prepped_own_content ) {
			free( content );
		}
	}

	return ( !delayed ) ? 1: 2;
}


//Compare the path against the instance routes
int find_matching_route ( struct luadata_t *l ) {
	ztable_t *t = NULL;
	struct route_t p =  { 0 };

	if ( lt_geti( l->zconfig, "routes" ) > -1 ) { 
		//Create a mini table
		p.src = t = lt_copy_by_key( l->zconfig, "routes" );

		//Loop through the routes...
		lt_exec_complex( t, 1, t->count, &p, make_route_list );

		//Loop through the routes
		l->pp.depth = 1;
		int notfound = 1;
		for ( struct iroute_t **lroutes = p.iroute_tlist; *lroutes; lroutes++ ) {
			if ( route_resolve( l->apath, (*lroutes)->route ) ) {
				memcpy( (void *)l->rroute, (*lroutes)->route, strlen( (*lroutes)->route ) );
				ztable_t * croute = lt_copy_by_index( t, (*lroutes)->index );
				lt_exec_complex( croute, 1, croute->count, &l->pp, make_mvc_list );
				l->zroute = croute;
				notfound = 0;
				lt_free( t ), free( t );
				break;
			}
		}

		//Free the route list
		free_route_list( p.iroute_tlist );
		return !notfound;
	}

	return 0;
}


int has_views( struct imvc_t **list ) {
	for ( struct imvc_t **l = list; l && *l; l++ ) {
		if ( *(*l)->file == 'v' ) return 1;
	}
	return 0;
}


//The entry point for a Lua application
const int filter_lua( const server_t *serv, conn_t *conn ) {

	//Define variables and error positions...
	ztable_t zc = {0}, zm = {0};
	struct luadata_t ld = {0};
	int clen = 0, ccount = 0, tcount = 0, model = 0, view = 0;
	unsigned char *content = NULL;

	//Prepare the response
	memset( conn->res, 0, sizeof( zhttp_t ) );

	//Initialize Lua data structure
	ld.req = conn->req; 
	ld.res = conn->res;
	ld.res->atype = ZHTTP_MESSAGE_MALLOC;
	memcpy( (void *)ld.root, conn->config->dir, strlen( conn->config->dir ) );

	//Then initialize the Lua state
	if ( !( ld.state = luaL_newstate() ) ) {
		return http_error( conn->res, 500, "%s", "Failed to initialize Lua environment." );
	}

	//Load the standard libraries first
	luaL_openlibs( ld.state );

	//Then load the Hypno extensions 
	if ( !lua_loadlibs( ld.state, functions ) ) {
		free_ld( &ld );
		return http_error( conn->res, 500, "Failed to initialize Lua standard libs." ); 
	}

	//Then start loading our configuration
	if ( !load_lua_config( &ld ) ) {
		free_ld( &ld );
		return http_error( conn->res, 500, "%s\n", ld.err );
	}

	//Need to delegate to static handler when request points to one of the static paths
	if ( path_is_static( &ld ) ) {
		free_ld( &ld );
		return send_static( conn->res, ld.root, conn->req->path );
	}

	//req->path needs to be modified to return just the path without the ?
	if ( !getpath( conn->req->path, (char *)ld.apath, LD_LEN ) ) {
		free_ld( &ld );
		return http_error( conn->res, 500, "%s", "Failed to extract path info into Lua userspace - Path too long, try increasing LD_LEN to fix this." );
	}

	if ( !find_matching_route( &ld ) ) {
		free_ld( &ld );
		return http_error( conn->res, 404, "Couldn't find path at %s\n", ld.apath );
	}

	//Loop through the structure and add read-only structures to Lua, 
	//you could also add the libraries, but that is a different method
	for ( struct lua_readonly_t *t = lua_readonly; t->name; t++ ) {
		if ( !t->exec( &ld ) ) {
			free_ld( &ld );
			return http_error( conn->req, ld.status, ld.err );
		}
		lua_setglobal( ld.state, t->name );
	}

	//Set package path
	if ( lua_retglobal( ld.state, "package", LUA_TTABLE ) ) {
		//Get the path of whatever we're talking about
		char ppath[ PATH_MAX / 2 ] = { 0 }, cpath[ PATH_MAX / 2 ] = {0};
		const char *lpath = lua_getv( ld.state, "path", 1 );
		snprintf( ppath, sizeof( ppath ) - 1, extfmt, lpath, ld.root, ld.root );
		lua_pop( ld.state, lua_gettop( ld.state ) - 1 );

		const char *lcpath = lua_getv( ld.state, "cpath", 1 );
		snprintf( cpath, sizeof( cpath ) - 1, libcfmt, lcpath, ld.root );
		lua_pop( ld.state, lua_gettop( ld.state ) - 1 );

		//Re-add to the table
		#if 1
		lua_setstrstr( ld.state, "path", ppath, 1 );
		lua_setstrstr( ld.state, "cpath", cpath, 1 );
		#else
		lua_pushstring( ld.state, "path" );
		lua_pushstring( ld.state, ppath );
		lua_settable( ld.state, 1 );
		lua_pushstring( ld.state, "cpath" );
		lua_pushstring( ld.state, cpath );
		lua_settable( ld.state, 1 );
		#endif
		lua_setglobal( ld.state, "package" );
	}

	//Execute each model
	for ( struct imvc_t **m = ld.pp.imvc_tlist; m && *m; m++ ) {
		//Define
		char err[2048] = {0}, msymname[1024] = {0}, mpath[ 2192 ] = {0};

		//Check for a file
		if ( *(*m)->file == 'a' ) {
			//Open the file that will execute the model
			if ( *(*m)->base != '@' )
				snprintf( mpath, sizeof( mpath ), "%s/%s", ld.root, (*m)->file );
			else {
				snprintf( mpath, sizeof( mpath ), "%s/%s/%s.%s", ld.root, "app", ld.aroute, (*m)->ext );
			}

			//...
			FPRINTF( "Executing model %s\n", mpath );
			if ( !lua_exec_file( ld.state, mpath, ld.err, sizeof( ld.err ) ) ) {
				free_ld( &ld );
				return http_error( conn->res, 500, "Error occurred: %s", ld.err );
			}

			//Get name of model file in question 
			memcpy( msymname, &(*m)->file[4], strlen( (*m)->file ) - 8 );

			//Get a count of the values which came from the model
			tcount += ccount = lua_gettop( ld.state );

			//Merge previous models
			if ( tcount > 1 ) {
				lua_getglobal( ld.state, modelkey );
				( lua_isnil( ld.state, -1 ) ) ? lua_pop( ld.state, 1 ) : 0;
				lua_merge( ld.state );	
				lua_setglobal( ld.state, modelkey );
			} 
			else if ( ccount ) {
				lua_setglobal( ld.state, modelkey );
			}
			model = 1;
		}

		//Stop if the user specifies a 'response' table that's not empty...
		if ( lua_retglobal( ld.state, "response", LUA_TTABLE ) ) {
			FPRINTF( "Evaluating response table.\n" );
			int eres = return_as_response( &ld );

			if ( !eres ) {
				free_ld( &ld );
				FPRINTF( "Error when evaluating response table\n" );
				return http_error( conn->res, 500, ld.err );
			}
			else if ( eres == 1 ) {
				lua_pop( ld.state, 1 );
				free_ld( &ld );
				FPRINTF( "Content return completed\n" );
				return 1;
			}

			lua_pop( ld.state, 1 );
			FPRINTF( "Finished evaluating delayed response table...\n" );
		}
	}

	//Can we simply check if config exists in _G?
	if ( has_views( ld.pp.imvc_tlist ) && lua_retglobal( ld.state, configkey, LUA_TTABLE ) ) {
		FPRINTF( "Adding config...\n" );
		
		//Make a config key and do some stuff...
		lua_newtable( ld.state );
		lua_pushstring( ld.state, configkey );
		lua_pushnil( ld.state );
		lua_copy( ld.state, 1, 4 );
		lua_remove( ld.state, 1 ); 
		lua_settable( ld.state, 1 );

		//...
		lua_getglobal( ld.state, modelkey );
	#if 0
		lua_isnil( ld.state, -1 ) ? lua_pop( ld.state, 1 ) : 0;
	#else
		lua_isnil( ld.state, -1 ) ? lua_pop( ld.state, 1 ) : lua_merge( ld.state );
	#endif

		//Add it back to the model after successful merge
		lua_setglobal( ld.state, modelkey );
		FPRINTF( "Config done.\n" );
	}

	//Could be either a table or string... so account for this
	if ( lua_retglobal( ld.state, modelkey, LUA_TTABLE ) ) {
		FPRINTF( "Adding model...\n" );
		const char **c = ctype_tags;
		char tkey[ 1024 ] = { 0 }, *key = lt_retkv( ld.zroute, 0 )->key.v.vchar;
		int count = lua_count( ld.state, 1 ), ksize = sizeof( tkey );

		//Initialize a table
		if ( !( ld.zmodel = lt_make( ( count = ( count < 1 ) ? 16 : count ) * 2 ) ) ) {
			free_ld( &ld );
			return http_error( conn->res, 500, "Couldn't allocate table." );
		}
		
		//Convert the model
		if ( !lua_to_ztable( ld.state, 1, ld.zmodel ) ) {
			free_ld( &ld );
			return http_error( conn->res, 500, "Error in model conversion." );
		}

		//TODO: Check for an inherited content-type then a default content-type
		for ( int index = -1; *c; c++ ) {
			memset( tkey, 0, ksize ), snprintf( tkey, ksize - 1, "%s.%s", key, *c );
			if ( ( index = lt_geti( ld.zroute, tkey ) ) > -1 ) {
				//Get Content-Type
				char *ctype = lt_text_at( ld.zroute, index );
				for ( ctype_t *cc = ctypes_serializable; cc->ctypename != NULL; cc++ ) {
					if ( !strcasecmp( ctype, cc->ctypename ) ) {
						//Throw your own response in JSON?
						if ( !return_as_serializable( &ld, cc ) ) {
							char err[ LD_ERRBUF_LEN ] = { 0 };
							memcpy( err, ld.err, strlen( ld.err ) );
							free_ld( &ld );
							return http_error( conn->res, 500, "%s", err );
						}
						free_ld( &ld );
						return 1;
					}
				}
			}
		}

		//Finally, check if there is a view specified 
		memset( tkey, 0, ksize ), snprintf( tkey, ksize - 1, "%s.%s", key, "view" );
		if ( lt_geti( ld.zroute, tkey ) == -1 ) {
			if ( !return_as_serializable( &ld, &ctypes_serializable[ CTYPE_JSON ] ) ) {
				char err[ LD_ERRBUF_LEN ] = { 0 };
				memcpy( err, ld.err, strlen( ld.err ) );
				free_ld( &ld );
				return http_error( conn->res, 500, "%s", err );
			}
			free_ld( &ld );
			return 1;
		}
		lua_pop( ld.state, 1 );
		lt_lock( ld.zmodel );
		FPRINTF( "Done with model...\n" );
	}

	//TODO: routes with no special keys need not be added
	//Load all views
	for ( struct imvc_t **v = ld.pp.imvc_tlist; v && *v; v++ ) {
		if ( *(*v)->file == 'v' ) {
			int len = 0, renlen = 0;
			char vpath[ 2192 ] = {0};
			unsigned char *src, *render;
			zRender * rz = zrender_init();
			zrender_set_default_dialect( rz );
			zrender_set_fetchdata( rz, ld.zmodel );
			
			if ( *(*v)->base != '@' )
				snprintf( vpath, sizeof( vpath ), "%s/%s", ld.root, (*v)->file );
			else {
				snprintf( vpath, sizeof( vpath ), "%s/%s/%s.%s", ld.root, "views", ld.aroute, (*v)->ext );
			}

			FPRINTF( "Loading view at: %s\n", vpath );
			if ( !( src = read_file( vpath, &len, ld.err, LD_ERRBUF_LEN )	) || !len ) {
				zrender_free( rz ), free( src ), free_ld( &ld );
				return http_error( conn->res, 500, "Error opening view '%s': %s", vpath, ld.err );
			}

			if ( !( render = zrender_render( rz, src, strlen((char *)src), &renlen ) ) ) {
				char errbuf[ 2048 ] = { 0 };
				snprintf( errbuf, sizeof( errbuf ), "%s", rz->errmsg );
				zrender_free( rz ), free( src ), free_ld( &ld );
				return http_error( conn->res, 500, "%s", errbuf );
			}

			zhttp_append_to_uint8t( &content, &clen, render, renlen ); 
			zrender_free( rz ), free( render ), free( src );
			view = 1;
		}
	}

	//Fail out when neither model or view is specified
	if ( !model && !view ) {
		//free( content );
		free_ld( &ld );
		return http_error( conn->res, 500, "Neither model nor view was specified for '/%s'.", ld.aroute );
	}

	//Set needed info for the response structure
	conn->res->clen = clen;
	http_set_status( conn->res, 200 );
	#if 0
	// TODO: Something wonky lurks here.
	http_set_ctype( res, ctype_def );
	#else
	char *ctype = zhttp_dupstr( ctype_def );
	conn->res->ctype = ctype;
	#endif
	http_set_content( conn->res, content, clen ); 

	//Return the finished message if we got this far
	if ( !http_finalize_response( conn->res, ld.err, LD_ERRBUF_LEN ) ) {
		snprintf( conn->err, sizeof( conn->err ), 
			"Failed to finalize HTTP response: %s", ld.err );
		FPRINTF( "Failed to finalize HTTP response: %s", ld.err );
		free_ld( &ld );
		return 0;
	}

	//Destroy model & Lua
	free( ctype );
	free_ld( &ld );
	free( content );
	return 1;
}



#ifdef RUN_MAIN
int main ( int argc, char *argv[] ) {
	zhttp_t req = {0}, res = {0};
	char err[ 2048 ] = { 0 };

	//Populate the request structure.  Normally, one will never populate this from scratch
	req.path = zhttp_dupstr( "/books" );
	req.ctype = zhttp_dupstr( "text/html" );
	req.host = zhttp_dupstr( "example.com" );
	req.method = zhttp_dupstr( "GET" );
	req.protocol = zhttp_dupstr( "HTTP/1.1" );

	//Assemble a message from here...
	if ( !http_finalize_request( &req, err, sizeof( err ) ) ) {
		fprintf( stderr, "%s\n", err );
		return 1; 
	}

	//run the handler
	if ( !lua_handler( &req, &res ) ) {
		fprintf( stderr, "lmain: HTTP funct failed to execute\n" );
		write( 2, res.msg, res.mlen );
		http_free_request( &req );
		http_free_response( &res );
		return 1;
	}

	//Destroy res, req and anything else allocated
	http_free_request( &req );
	http_free_response( &res );

	//After we're done, look at the response
	return 0;
}
#endif
