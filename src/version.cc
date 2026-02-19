#include "version.hh"
namespace Version {
	uint64_t    constexpr Cache = 31      ; // a501eb65e45d8dd6538caffe6ee999e6
	uint64_t    constexpr Codec = 2       ; // 603a8fc6deb9a767ed591521309aef40
	uint64_t    constexpr Repo  = 27      ; // dd9f62e349f7b59a7a5c56915d89970d
	uint64_t    constexpr Job   = 17      ; // e9815ab26bf928f163f771390de178e6
	const char* const     Major = "26.02" ;
	uint64_t    constexpr Tag   = 0       ;
}

// ********************************************
// * Cache : a501eb65e45d8dd6538caffe6ee999e6 *
// ********************************************
//
//	// START_OF_VERSIONING CACHE REPO JOB
//		res << ':' ;
//		if (auto_mkdir     ) res << 'm' ;
//		if (deps_in_system ) res << 'X' ;
//		if (disabled       ) res << 'd' ;
//		if (ignore_stat    ) res << 'i' ;
//		if (mount_chroot_ok) res << 'M' ;
//		if (readdir_ok     ) res << 'D' ;
//		switch (file_sync) {
//			case FileSync::Auto : res << "sa" ; break ;
//			case FileSync::None : res << "sn" ; break ;
//			case FileSync::Dir  : res << "sd" ; break ;
//			case FileSync::Sync : res << "ss" ; break ;
//		DF} //! NO_COV
//		switch (lnk_support) {
//			case LnkSupport::None : res << "ln" ; break ;
//			case LnkSupport::File : res << "lf" ; break ;
//			case LnkSupport::Full : res << "la" ; break ;
//		DF} //! NO_COV                                                   empty_ok
//		res <<':'<< '"'<<mk_printable<'"'>(                  fqdn               )<<'"' ;
//		res <<':'<< '"'<<mk_printable<'"'>(                  tmp_dir_s          )<<'"' ;
//		res <<':'<< '"'<<mk_printable<'"'>(                  repo_root_s        )<<'"' ;
//		res <<':'<< '"'<<mk_printable<'"'>(                  sub_repo_s         )<<'"' ;
//		res <<':'<<      mk_printable     (                  src_dirs_s  ,false )      ;
//		res <<':'<<      mk_printable     (mk_vmap<::string>(codecs     ),false )      ;
//		res <<':'<<      mk_printable     (                  views_s     ,false )      ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE
//		{ ::string file=g_store_dir_s+"key"       ; nfs_guard.access(file) ; _g_key_file      .init( file , !read_only ) ; }
//		{ ::string file=g_store_dir_s+"job_name"  ; nfs_guard.access(file) ; _g_job_name_file .init( file , !read_only ) ; }
//		{ ::string file=g_store_dir_s+"node_name" ; nfs_guard.access(file) ; _g_node_name_file.init( file , !read_only ) ; }
//		{ ::string file=g_store_dir_s+"job"       ; nfs_guard.access(file) ; _g_job_file      .init( file , !read_only ) ; }
//		{ ::string file=g_store_dir_s+"run"       ; nfs_guard.access(file) ; _g_run_file      .init( file , !read_only ) ; }
//		{ ::string file=g_store_dir_s+"node"      ; nfs_guard.access(file) ; _g_node_file     .init( file , !read_only ) ; }
//		{ ::string file=g_store_dir_s+"nodes"     ; nfs_guard.access(file) ; _g_nodes_file    .init( file , !read_only ) ; }
//		{ ::string file=g_store_dir_s+"crcs"      ; nfs_guard.access(file) ; _g_crcs_file     .init( file , !read_only ) ; }
//		// END_OF_VERSIONING
//				// START_OF_VERSIONING CACHE
//				rename( rf+"-data" , run_name+"-data" , {.nfs_guard=&nfs_guard} ) ; data_moved = true ;
//				rename( rf+"-info" , run_name+"-info" , {.nfs_guard=&nfs_guard} ) ;
//				// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE REPO
//		FileSig::FileSig(FileInfo const& fi) : FileSig{fi.tag()} {
//			switch (fi.tag()) {
//				case FileTag::None    :
//				case FileTag::Unknown :
//				case FileTag::Dir     :                                                                break ;
//				case FileTag::Empty   : _val |= fi.date.val() & msb_msk<Ddate::Tick>(NBits<FileTag>) ; break ; // improve traceability when size is predictible, 8ns granularity is more than enough
//				case FileTag::Lnk     :
//				case FileTag::Reg     :
//				case FileTag::Exe     : {
//					Xxh h ;
//					h    += fi.date                       ;
//					h    += fi.sz                         ;
//					_val |= +h.digest() << NBits<FileTag> ;
//				} break ;
//			DF}                                                                                                // NO_COV
//		}
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO
//		template<uint8_t Sz> _Crc<Sz>::_Crc(::string const& filename) {
//			// use low level operations to ensure no time-of-check-to time-of-use hasards as crc may be computed on moving files
//			self = None ;
//			if ( AcFd fd{filename,{.flags=O_RDONLY|O_NOFOLLOW,.err_ok=true}} ; +fd ) {
//				FileInfo fi { fd } ;
//				switch (fi.tag()) {
//					case FileTag::Empty :
//						self = Empty ;
//					break ;
//					case FileTag::Reg :
//					case FileTag::Exe : {
//						_Xxh<Sz> ctx { fi.tag() }                   ;
//						::string buf ( ::min(DiskBufSz,fi.sz) , 0 ) ;
//						for( size_t sz=fi.sz ;;) {
//							ssize_t cnt = ::read( fd , buf.data() , buf.size() ) ;
//							if      (cnt> 0) ctx += ::string_view(buf.data(),cnt) ;
//							else if (cnt==0) break ;                                // file could change while crc is being computed
//							else switch (errno) {
//								#if EWOULDBLOCK!=EAGAIN
//									case EWOULDBLOCK :
//								#endif
//								case EAGAIN :
//								case EINTR  : continue                                       ;
//								default     : throw "I/O error while reading file "+filename ;
//							}
//							SWEAR( cnt>0 , cnt ) ;
//							if (size_t(cnt)>=sz) break ;
//							sz -= cnt ;
//						}
//						self = ctx.digest() ;
//					} break ;
//				DN}
//			} else if ( ::string lnk_target=read_lnk(filename) ; +lnk_target ) {
//				_Xxh<Sz> ctx { FileTag::Lnk } ;
//				ctx += ::string_view( lnk_target.data() , lnk_target.size() ) ;     // no need to compute crc on size as would be the case with ctx += lnk_target
//				self = ctx.digest() ;
//			}
//		}
//		// END_OF_VERSIONING
//					// START_OF_VERSIONING REPO CACHE CODEC
//					static constexpr MatchFlags IncPhony { .tflags{Tflag::Incremental,Tflag::Phony,Tflag::Target} } ;
//					stems = {
//						{ "File" ,     ".+"                                            } // static
//					,	{ "Ctx"  , cat("[^",CodecSep,"]*")                             } // star
//					,	{ "Code" ,     "[^/]*"                                         } // .
//					,	{ "Val"  , cat("[A-Za-z0-9_-]{",Codec::CodecCrc::Base64Sz,'}') } // .      /!\ - must be first or last char in []
//					} ;
//					n_static_stems = 1 ;
//					//
//					static ::string pfx = Codec::CodecFile::s_pfx_s() ;
//					job_name = cat(pfx,_stem_mrkr(0/*File*/)) ;
//					matches  = { //!                             File                        Ctx                                                                 File Ctx Code/Val
//						{ "DECODE" , {.pattern=cat(pfx,_stem_mrkr(0 ),'/',CodecSep,_stem_mrkr(1),'/',_stem_mrkr(2/*Code*/),DecodeSfx),.flags=IncPhony,.captures={true,true,true  }} } // star target
//					,	{ "ENCODE" , {.pattern=cat(pfx,_stem_mrkr(0 ),'/',CodecSep,_stem_mrkr(1),'/',_stem_mrkr(3/*Val */),EncodeSfx),.flags=IncPhony,.captures={true,true,true  }} } // .
//					} ;
//					matches_iotas[true/*star*/][+MatchKind::Target] = { 0/*start*/ , VarIdx(matches.size())/*end*/ } ;
//					//
//					deps_attrs.spec.deps = {
//						{ "CODEC_FILE" , {.txt=_stem_mrkr(VarCmd::Stem,0/*File*/),.dflags=DflagsDfltStatic,.extra_dflags=ExtraDflagsDfltStatic} }
//					} ;
//					// END_OF_VERSIONING
//			// START_OF_VERSIONING CACHE JOB REPO
//			SWEAR( is_lcl(node) , node ) ;
//			size_t pos1 = s_pfx_s().size()            ;
//			size_t pos3 = node.rfind('/'            ) ; SWEAR( pos3!=Npos && pos1<pos3                      , node,pos1,     pos3 ) ;
//			size_t pos2 = node.rfind(CodecSep,pos3-1) ; SWEAR( pos2!=Npos && pos1<pos2 && node[pos2-1]=='/' , node,pos1,pos2,pos3 ) ;
//			//
//			file = node.substr(pos1,pos2-pos1) ; file.pop_back() ;
//			pos3++/* / */ ;
//			if      (node.ends_with(DecodeSfx)) { size_t sz = node.size()-DecodeSfxSz-pos3 ;                                    _code_val_crc = parse_printable<'/'>(node.substr(pos3,sz))    ; }
//			else if (node.ends_with(EncodeSfx)) { size_t sz = node.size()-EncodeSfxSz-pos3 ; SWEAR(sz==CodecCrc::Base64Sz,sz) ; _code_val_crc = CodecCrc::s_from_base64(node.substr(pos3,sz)) ; }
//			else                                  FAIL(node) ;
//			pos2++/*CodecSep*/ ;
//			ctx = parse_printable<CodecSep>( node.substr( pos2 , pos3-1/* / */-pos2 ) ) ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING CACHE JOB REPO CODEC
//			SWEAR( !is_lcl(node) , node ) ;
//			size_t pos3 = node.rfind('/')        ; SWEAR( pos3!=Npos && 0<pos3              , node,pos3            ) ;
//			size_t pos2 = ext_codec_dir_s.size() ; SWEAR( node.starts_with(ext_codec_dir_s) , node,ext_codec_dir_s ) ;
//			throw_unless( substr_view(node,pos2).starts_with("tab/") , node,"is not a codec file" ) ;
//			//
//			file = node.substr(0,pos2) ;
//			pos3++/* / */ ;
//			if      (node.ends_with(DecodeSfx)) { size_t sz = node.size()-DecodeSfxSz-pos3 ;                                    _code_val_crc = parse_printable<'/'>(node.substr(pos3,sz))    ; }
//			else if (node.ends_with(EncodeSfx)) { size_t sz = node.size()-EncodeSfxSz-pos3 ; SWEAR(sz==CodecCrc::Base64Sz,sz) ; _code_val_crc = CodecCrc::s_from_base64(node.substr(pos3,sz)) ; }
//			else                                  FAIL(node) ;
//			pos3 -= 1/* / */                        ;
//			pos2 += 4/*tab/ */                      ;
//			ctx   = node.substr( pos2 , pos3-pos2 ) ;
//			// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO CODEC
//		::string CodecFile::ctx_dir_s(bool tmp) const {
//			::string res = s_dir_s(file,tmp) ;
//			if (is_dir_name(file)) res << "tab/"  <<                       ctx  ;
//			else                   res << CodecSep<<mk_printable<CodecSep>(ctx) ;
//			/**/                   res << '/'                                   ;
//			return res ;
//		}
//		::string CodecFile::name(bool tmp) const {
//			::string res = ctx_dir_s(tmp) ;
//			if (is_encode()) res << val_crc().base64()       <<EncodeSfx ;
//			else             res << mk_printable<'/'>(code())<<DecodeSfx ;
//			return res ;
//		}
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE REPO JOB
//		bool                             auto_mkdir       = false ;                   // if true  <=> auto mkdir in case of chdir
//		bool                             deps_in_system   = false ;                   // if false <=> system files are simple and considered as deps
//		bool                             disabled         = false ;                   // if false <=> no automatic report
//		bool                             ignore_stat      = false ;                   // if true  <=> stat-like syscalls do not trigger dependencies
//		bool                             mount_chroot_ok  = false ;
//		bool                             readdir_ok       = false ;                   // if true  <=> allow reading local non-ignored dirs
//		::string                         fast_report_pipe ;                           // pipe to report accesses, faster than sockets, but does not allow replies
//		KeyedService                     service          ;
//		::string                         sub_repo_s       ;                           // relative to repo_root_s
//		::umap_s<Codec::CodecRemoteSide> codecs           ;
//		::vmap_s<::vector_s>             views_s          ;
//		// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE
//	struct LruEntry {
//		friend ::string& operator+=( ::string& , LruEntry const& ) ;
//		// accesses
//		bool operator+() const { return +newer || +older ; }
//		// services
//		bool/*first*/ insert_top( LruEntry      & hdr , Crun , LruEntry CrunData::* lru )       ;
//		bool/*last*/  erase     ( LruEntry      & hdr ,        LruEntry CrunData::* lru )       ;
//		void          mv_to_top ( LruEntry      & hdr , Crun , LruEntry CrunData::* lru )       ;
//		void          chk       ( LruEntry const& hdr , Crun , LruEntry CrunData::* lru ) const ;
//		// data
//		Crun newer ; // for headers : oldest
//		Crun older ; // for headers : newest
//	} ;
//	// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE
//		CrunIdx ref_cnt = 0 ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE
//		LruEntry lru       ;
//		uint16_t n_runs    = 0 ;
//		VarIdx   n_statics = 0 ;
//	private :
//		CjobName _name ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE
//		LruEntry     lrus[NRates] ;
//		Disk::DiskSz total_sz     = 0  ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE
//		Time::Pdate  last_access ;
//		Disk::DiskSz sz          = 0                ;                                                   // size occupied by run
//		LruEntry     glb_lru     ;                                                                      // global LRU within rate
//		LruEntry     job_lru     ;                                                                      // job LRU
//		Cjob         job         ;
//		Cnodes       deps        ;                                                                      // owned sorted by (is_static,existing,idx)
//		Ccrcs        dep_crcs    ;                                                                      // owned crcs for static and existing deps
//		Ckey         key         ;                                                                      // identifies origin (repo+git_sha1)
//		Rate         rate        = 0    /*garbage*/ ;
//		bool         key_is_last = false/*.      */ ;                                                   // 2 runs may be stored for each key : the first and the last
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE
//		CrunIdx ref_cnt = 0 ;
//	private :
//		CnodeName _name ;
//		// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE
//	//                                           ThreadKey header    index       n_index_bits        key    data        misc
//	using CkeyFile      = Store::SinglePrefixFile< '='   , void    , Ckey      , NCkeyIdxBits      , char , CkeyData                          > ;
//	using CjobNameFile  = Store::SinglePrefixFile< '='   , void    , CjobName  , NCjobNameIdxBits  , char , Cjob                              > ;
//	using CnodeNameFile = Store::SinglePrefixFile< '='   , void    , CnodeName , NCnodeNameIdxBits , char , Cnode                             > ;
//	using CjobFile      = Store::AllocFile       < '='   , void    , Cjob      , NCjobIdxBits      ,        CjobData  , 0/*Mantissa*/         > ;
//	using CrunFile      = Store::AllocFile       < '='   , CrunHdr , Crun      , NCrunIdxBits      ,        CrunData                          > ;
//	using CnodeFile     = Store::AllocFile       < '='   , void    , Cnode     , NCnodeIdxBits     ,        CnodeData , 0/*Mantissa*/         > ;
//	using CnodesFile    = Store::VectorFile      < '='   , void    , Cnodes    , NCnodesIdxBits    ,        Cnode     , CnodeIdx , 4/*MinSz*/ > ;
//	using CcrcsFile     = Store::VectorFile      < '='   , void    , Ccrcs     , NCcrcsIdxBits     ,        Hash::Crc , CnodeIdx , 4/*.    */ > ;
//	// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE
//
//		// used for cache efficiency
//		// rate=0 means max_rate as per config
//		// +1 means job took 13.3% more time per byte of generated data
//		using Rate = uint8_t ;
//
//		// can be tailored to fit needs
//		static constexpr uint8_t NCkeyIdxBits      = 32 ;
//		static constexpr uint8_t NCjobNameIdxBits  = 32 ;
//		static constexpr uint8_t NCnodeNameIdxBits = 32 ;
//		static constexpr uint8_t NCjobIdxBits      = 32 ;
//		static constexpr uint8_t NCrunIdxBits      = 32 ;
//		static constexpr uint8_t NCnodeIdxBits     = 32 ;
//		static constexpr uint8_t NCnodesIdxBits    = 32 ;
//		static constexpr uint8_t NCcrcsIdxBits     = 32 ;
//
//		// END_OF_VERSIONING
//			// START_OF_VERSIONING CACHE
//			Disk::DiskSz max_sz           = 0     ;
//			Disk::DiskSz max_rate         = 1<<30 ; // in B/s, maximum rate (total_sz/exe_time) above which run is not cached
//			uint16_t     max_runs_per_job = 100   ;
//			FileSync     file_sync        = {}    ;
//			mode_t       umask            = -1    ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING CACHE
//			CacheRpcProc                      proc           = {}    ;
//			::string                          repo_key       = {}    ; // if proc = Config
//			StrId<CjobIdx>                    job            = {}    ; // if proc = Download | Commit
//			::vmap<StrId<CnodeIdx>,DepDigest> repo_deps      = {}    ; // if proc = Download | Commit
//			uint32_t                          conn_id        = 0     ; // if proc =            Upload | Dismiss, when from job_exec
//			bool                              override_first = false ; // if proc =            Commit          , replace first slot if last slot does not exist, used after a maybe_rerun
//			Disk::DiskSz                      reserved_sz    = 0     ; // if proc =            Upload
//			Disk::DiskSz                      total_z_sz     = 0     ; // if proc =            Commit
//			Disk::DiskSz                      job_info_sz    = 0     ; // if proc =            Commit
//			Time::CoarseDelay                 exe_time       = {}    ; // if proc =            Commit
//			CacheUploadKey                    upload_key     = 0     ; // if proc =            Commit | Dismiss
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING CACHE
//			CacheRpcProc       proc        = {}    ;
//			CacheConfig        config      = {}    ;                                                                // if proc = Config
//			uint32_t           conn_id     = 0     ;                                                                // if proc = Config  , id to be repeated by upload requests
//			CacheHitInfo       hit_info    = {}    ;                                                                // if proc = Download
//			CkeyIdx            key         = 0     ;                                                                // if proc = Download
//			bool               key_is_last = false ;                                                                // if proc = Download
//			::vector<CnodeIdx> dep_ids     = {}    ;                                                                // if proc = Download, idx of corresponding deps in Req that were passed by name
//			CjobIdx            job_id      = 0     ;                                                                // if proc = Download, idx of corresponding job  in Req if passed by name
//			CacheUploadKey     upload_key  = 0     ;                                                                // if proc = Upload
//			::string           msg         = {}    ;                                                                // if proc = Upload and upload_key=0
//			// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class Access : uint8_t {                                                         // in all cases, dirs are deemed non-existing
//		Lnk                                                                               // file is accessed with readlink              , regular files are deemed non-existing
//	,	Reg                                                                               // file is accessed with open                  , symlinks      are deemed non-existing
//	,	Stat                                                                              // file is sensed for existence only
//	,	Err                                                                               // dep is sensitive to status (ok/err)
//	//
//	// aliases
//	,	Data = Err                                                                        // <= Data means refer to file content
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class JobInfoKind : uint8_t {
//		None
//	,	Start
//	,	End
//	,	DepCrcs
//	} ;
//	// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO CACHE
//			CacheIdx            cache_idx1   = 0     ; // 0 means no cache
//			::vmap_s<DepDigest> deps         = {}    ;
//			bool                live_out     = false ;
//			uint8_t             nice         = -1    ; // -1 means not specified
//			Time::CoarseDelay   pressure     = {}    ;
//			JobReason           reason       = {}    ;
//			Tokens1             tokens1      = 0     ;
//			BackendTag          used_backend = {}    ; // tag actually used (possibly made local because asked tag is not available)
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO CACHE
//			Hash::Crc        rule_crc_cmd = {} ;
//			::vector_s       stems        = {} ;
//			Time::Pdate      eta          = {} ;
//			SubmitInfo       submit_info  = {} ;
//			::vmap_ss        rsrcs        = {} ;
//			JobStartRpcReq   pre_start    = {} ;
//			JobStartRpcReply start        = {} ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO CACHE
//			JobInfoStart                            start    ;
//			JobEndRpcReq                            end      ;
//			::vector<::pair<Hash::Crc,bool/*err*/>> dep_crcs ; // optional, if not provided in end.digest.deps
//			// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class LnkSupport : uint8_t {
//		None
//	,	File
//	,	Full
//	} ;
//	// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO
//		FileSync   file_sync   = {}               ;
//		LnkSupport lnk_support = LnkSupport::Full ; // by default, be pessimistic
//		::string   repo_root_s = {}               ;
//		::string   tmp_dir_s   = {}               ;
//		::vector_s src_dirs_s  = {}               ;
//		// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//
//	// NXxxBits are used to dimension address space, and hence max number of objects for each category.
//	// can be tailored to fit neeeds
//	static constexpr uint8_t NCacheIdxBits    =  8 ; // used to caches
//	static constexpr uint8_t NCodecIdxBits    = 32 ; // used to store code <-> value associations in lencode/ldecode
//	static constexpr uint8_t NDepsIdxBits     = 32 ; // used to index deps
//	static constexpr uint8_t NJobIdxBits      = 30 ; // 2 guard bits
//	static constexpr uint8_t NJobNameIdxBits  = 32 ; // used to index Job names
//	static constexpr uint8_t NJobTgtsIdxBits  = 32 ; // JobTgts are used to store job candidate for each Node, so this Idx is a little bit larget than NodeIdx
//	static constexpr uint8_t NNodeIdxBits     = 31 ; // 1 guard bit, there are a few targets per job, so this idx is a little bit larger than JobIdx
//	static constexpr uint8_t NNodeNameIdxBits = 32 ; // used to index Node names
//	static constexpr uint8_t NPsfxIdxBits     = 32 ; // each rule appears in a few Psfx slots, so this idx is a little bit larger than ruleTgtsIdx
//	static constexpr uint8_t NReqIdxBits      =  8 ;
//	static constexpr uint8_t NRuleIdxBits     = 16 ;
//	static constexpr uint8_t NRuleCrcIdxBits  = 32 ;
//	static constexpr uint8_t NRuleStrIdxBits  = 32 ; // used to index serialized Rule description
//	static constexpr uint8_t NRuleTgtsIdxBits = 32 ;
//	static constexpr uint8_t NTargetsIdxBits  = 32 ; // used to index targets
//
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//
//	// can be tailored to fit neeeds
//	using VarIdx = uint8_t ; // used to index stems, targets, deps & rsrcs within a Rule
//
//	// ids
//	// can be tailored to fit neeeds
//	using SmallId = uint32_t ; // used to identify running jobs, could be uint16_t if we are sure that there cannot be more than 64k jobs running at once
//	using SeqId   = uint64_t ; // used to distinguish old report when a job is relaunched, may overflow as long as 2 job executions have different values if the 1st is lost
//
//	// type to hold the dep depth used to track dep loops
//	// can be tailored to fit neeeds
//	using DepDepth = uint16_t ;
//
//	// job tokens
//	// can be tailored to fit neeeds
//	using Tokens1 = uint8_t ; // store number of tokens-1 (so tokens can go from 1 to 256)
//
//	// maximum number of rule generation before a Job/Node clean up is necessary
//	// can be tailored to fit neeeds
//	using MatchGen = uint8_t ;
//
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	// PER_AUTODEP_METHOD : add entry here
//	// >=Ld means a lib is pre-loaded (through LD_AUDIT or LD_PRELOAD)
//	// by default, use a compromize between speed an reliability
//	enum class AutodepMethod : uint8_t {
//		None
//	,	Ptrace
//	#if HAS_LD_AUDIT
//		,	LdAudit
//	#endif
//	,	LdPreload
//	,	LdPreloadJemalloc
//	// aliases
//	#if HAS_LD_AUDIT
//		,	Ld   = LdAudit
//		,	Dflt = LdAudit
//	#else
//		,	Ld   = LdPreload
//		,	Dflt = LdPreload
//	#endif
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class BackendTag : uint8_t { // PER_BACKEND : add a tag for each backend
//		Unknown                       // must be first
//	,	Local
//	,	Sge
//	,	Slurm
//	//
//	// aliases
//	,	Dflt   = Local
//	,	Remote = Sge                  // if >=Remote, backend is remote
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class CacheHitInfo : uint8_t {
//		Hit                             // cache hit
//	,	Match                           // cache matches, but not hit (some deps are missing, hence dont know if hit or miss)
//	,	BadDeps
//	,	NoJob
//	,	NoRule
//	,	BadDownload
//	,	NoDownload
//	,	BadCache
//	,	NoCache
//	// aliases
//	,	Miss = BadDeps                  // >=Miss means cache miss
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class ChrootAction : uint8_t {
//		ResolvConf                      // /etc/resolv.conf is copied from native env to chroot'ed env
//	,	UserName                        // user and root and their groups have a name, existing ones are not preserved
//	} ;
//	using ChrootActions = BitMap<ChrootAction> ;
//
//	// START_OF_VERSIONING REPO CACHE
//	enum class FileActionTag : uint8_t {
//		Src                              // file is src, no action
//	,	None                             // same as unlink except expect file not to exist
//	,	Unlink                           // used in ldebug, so it cannot be Unlnk
//	,	UnlinkWarning                    // .
//	,	UnlinkPolluted                   // .
//	,	Uniquify
//	,	Mkdir
//	,	Rmdir
//	//
//	// aliases
//	,	HasFile = Uniquify               // <=HasFile means action acts on file
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class JobMngtProc : uint8_t {
//		None
//	,	ChkDeps
//	,	ChkTargets // used in JobMngtRpcReply to signal a pre-existing target
//	,	DepDirect
//	,	DepVerbose
//	,	LiveOut
//	,	AddLiveOut // report missing live_out info (Req) or tell job_exec to send missing live_out info (Reply)
//	,	Heartbeat
//	,	Kill
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class JobRpcProc : uint8_t {
//		None
//	,	Start
//	,	ReportStart
//	,	GiveUp      // Req (all if 0) was killed and job was not (either because of other Req's or it did not start yet)
//	,	End
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class JobReasonTag : uint8_t {           // see explanations in table below
//		None
//	,	Retry                                     // job is retried in case of error      if asked so by user
//	,	LostRetry                                 // job is retried in case of lost_error if asked so by user
//	//	with reason
//	,	CacheMatch
//	,	OldErr
//	,	Rsrcs
//	,	PollutedTargets
//	,	ChkDeps
//	,	WasIncremental
//	,	Lost
//	,	WasLost
//	,	Force
//	,	Killed
//	,	Cmd
//	,	New
//	//	with node
//	,	BusyTarget
//	,	NoTarget
//	,	OldTarget
//	,	PrevTarget
//	,	PollutedTarget
//	,	ManualTarget
//	,	ClashTarget
//	// with dep
//	,	BusyDep                                   // job is waiting for an unknown dep
//	,	DepOutOfDate
//	,	DepTransient
//	,	DepUnlnked
//	,	DepUnstable
//	//	with error
//	,	DepOverwritten
//	,	DepDangling
//	,	DepErr
//	,	DepMissingRequired                        // this is actually an error
//	// with missing
//	,	DepMissingStatic                          // this prevents the job from being selected
//	//
//	// aliases
//	,	HasNode = BusyTarget                      // if >=HasNode <=> a node is associated
//	,	HasDep  = BusyDep                         // if >=HasDep  <=> a dep  is associated
//	,	Err     = DepOverwritten                  // if >=Err     <=> a dep  is in error
//	,	Missing = DepMissingStatic                // if >=Missing <=> a dep  is missing
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class Status : uint8_t { // result of job execution
//		New                       // job was never run
//	,	EarlyChkDeps              // dep check failed before job actually started
//	,	EarlyErr                  // job was not started because of error
//	,	EarlyLost                 // job was lost before starting     , retry
//	,	EarlyLostErr              // job was lost before starting     , do not retry
//	,	LateLost                  // job was lost after having started, retry
//	,	LateLostErr               // job was lost after having started, do not retry
//	,	Killed                    // job was killed
//	,	ChkDeps                   // dep check failed
//	,	CacheMatch                // cache just reported deps, not result
//	,	BadTarget                 // target was not correctly initialized or simultaneously written by another job
//	,	Ok                        // job execution ended successfully
//	,	RunLoop                   // job needs to be rerun but we have already run       it too many times
//	,	SubmitLoop                // job needs to be rerun but we have already submitted it too many times
//	,	Err                       // job execution ended in error
//	//
//	// aliases
//	,	Early   = EarlyLostErr    // <=Early means output has not been modified
//	,	Async   = Killed          // <=Async means job was interrupted asynchronously
//	,	Garbage = BadTarget       // <=Garbage means job has not run reliably
//	} ;
//	// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		FileActionTag tag    = {} ;
//		Tflags        tflags = {} ;
//		Hash::Crc     crc    = {} ; // expected (else, quarantine)
//		Disk::FileSig sig    = {} ; // .
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		Accesses accesses ;
//		Dflags   dflags   ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		NodeIdx node = 0                  ;
//		Tag     tag  = JobReasonTag::None ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		size_t            mem = 0  ; // in bytes
//		Time::CoarseDelay cpu = {} ;
//		Time::CoarseDelay job = {} ; // elapsed in job
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		using Base = ::variant< Hash::Crc , Disk::FileSig , Disk::FileInfo > ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		uint8_t       sz                        = 0          ;                                        //   8 bits, number of items in chunk following header (semantically before)
//		Dflags        dflags                    = DflagsDflt ;                                        // 7<8 bits
//		Accesses::Val accesses_      :N<Access> = 0          ;                                        //   4 bits
//		Accesses::Val chunk_accesses_:N<Access> = 0          ;                                        //   4 bits
//		bool          parallel       :1         = false      ;                                        //   1 bit , dep is parallel with prev dep
//		bool          is_crc         :1         = true       ;                                        //   1 bit
//		bool          hot            :1         = false      ;                                        //   1 bit , if true <= file date was very close from access date (within date granularity)
//		bool          err            :1         = false      ;                                        //   1 bit , if true <=> dep is in error (useful if IgnoreErr), valid only if is_crc
//		bool          create_encode  :1         = false      ;                                        //   1 bit , if true <=> dep has been created because of encode
//	private :
//		union {
//			Crc     _crc = {} ;                                                                       // ~45<64 bits
//			FileSig _sig ;                                                                            // ~40<64 bits
//		} ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		Tflags        tflags       = {}    ;
//		ExtraTflags   extra_tflags = {}    ;
//		bool          pre_exist    = false ; // if true <=> file was seen as existing while not incremental
//		bool          written      = false ; // if true <=> file was written or unlinked (if crc==None)
//		Crc           crc          = {}    ; // if None <=> file was unlinked, if Unknown => file is idle (not written, not unlinked)
//		Disk::FileSig sig          = {}    ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		CacheUploadKey           upload_key     = {}          ;
//		::vmap<Key,TargetDigest> targets        = {}          ;
//		::vmap<Key,DepDigest   > deps           = {}          ;                                // INVARIANT : sorted in first access order
//		::vector_s               refresh_codecs = {}          ;
//		Time::CoarseDelay        exe_time       = {}          ;
//		Status                   status         = Status::New ;
//		bool                     has_msg_stderr = false       ;                                // if true <= msg or stderr are non-empty in englobing JobEndRpcReq
//		bool                     incremental    = false       ;                                // if true <= job was run with existing incremental targets
//		// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO CACHE
//			::vector_s phys_s  = {} ;                                                              // (upper,lower...)
//			::vector_s copy_up = {} ;                                                              // dirs & files or dirs to create in upper (mkdir or cp <file> from lower...)
//			// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		::string            lmake_view_s = {} ;                                                    // absolute dir under which job sees open-lmake root dir (empty if unused)
//		::string            repo_view_s  = {} ;                                                    // absolute dir under which job sees repo root dir       (empty if unused)
//		::string            tmp_view_s   = {} ;                                                    // absolute dir under which job sees tmp dir             (empty if unused)
//		::vmap_s<ViewDescr> views        = {} ;                                                    // dir_s->descr, relative to sub_repo when _force_create=Maybe, else relative to repo_root
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		SeqId  seq_id = 0 ;
//		JobIdx job    = 0 ;
//		// END_OF_VERSIONING)
//		// START_OF_VERSIONING REPO CACHE
//		KeyedService service ; // where job_exec can be contacted (except addr which is discovered by server from peer_addr
//		::string     msg     ;
//		// END_OF_VERSIONING)
//		// START_OF_VERSIONING REPO CACHE
//		AutodepEnv                              autodep_env      ;
//		CacheRemoteSide                         cache            ;
//		bool                                    chk_abs_paths    = false               ;
//		ChrootInfo                              chroot_info      ;
//		::string                                cmd              ;
//		Time::Delay                             ddate_prec       ;
//		::vmap_s<::pair<DepDigest,ExtraDflags>> deps             ;                       // deps already accessed (always includes static deps), DepDigest does not include extra_dflags, so add them
//		::string                                domain_name      ;
//		::vmap_ss                               env              ;
//		::vector_s                              interpreter      ;                       // actual interpreter used to execute cmd
//		JobSpace                                job_space        ;
//		bool                                    keep_tmp         = false               ;
//		::string                                key              ;                       // key used to uniquely identify repo
//		bool                                    kill_daemons     = false               ;
//		::vector<uint8_t>                       kill_sigs        ;
//		bool                                    live_out         = false               ;
//		AutodepMethod                           method           = AutodepMethod::Dflt ;
//		Time::Delay                             network_delay    ;
//		uint8_t                                 nice             = 0                   ;
//		::string                                phy_lmake_root_s ;
//		::vmap_s<FileAction>                    pre_actions      ;
//		::string                                rule             ;                       // rule name
//		SmallId                                 small_id         = 0                   ;
//		::vmap<Re::Pattern,MatchFlags>          star_matches     ;                       // maps regexprs to flags
//		::vmap_s<MatchFlags>                    static_matches   ;                       // maps individual files to flags
//		bool                                    stderr_ok        = false               ;
//		::string                                stdin            ;
//		::string                                stdout           ;
//		Time::Delay                             timeout          ;
//		bool                                    use_script       = false               ;
//		Zlvl                                    zlvl             {}                    ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		JobDigest<>              digest        ;
//		::vmap_ss                dyn_env       ; // env variables computed in job_exec
//		Time::Pdate              end_date      ;
//		MsgStderr                msg_stderr    ;
//		::string                 os_info       ;
//		::string                 phy_tmp_dir_s ;
//		JobStats                 stats         ;
//		::string                 stdout        ;
//		Disk::DiskSz             total_sz      = 0 ;
//		Disk::DiskSz             total_z_sz    = 0 ;
//		::vector<UserTraceEntry> user_trace    ;
//		int                      wstatus       = 0 ;
//		// END_OF_VERSIONING)
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class Dflag : uint8_t { // flags for deps, recorded in server book-keeping
//		Critical                 // if modified, ignore following deps
//	,	Essential                // show when generating user oriented graphs
//	,	IgnoreError              // dont propagate error if dep is in error (Error instead of Err because name is visible from user)
//	,	Required                 // dep must be buildable (static deps are always required)
//	,	Static                   // is static dep, for internal use only
//	,	Codec                    // acquired with codec
//	,	Full                     // if false, dep is only necessary to compute resources
//	//
//	// aliases
//	,	NRule = Required         // number of Dflag's allowed in rule definition
//	,	NDyn  = Static           // number of Dflag's allowed in side flags
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class ExtraDflag : uint8_t { // flags for deps, not recorded in server book-keeping
//		Top
//	,	Ignore
//	,	ReaddirOk
//	,	NoStar                        // exclude flags from star patterns (common info for dep and target)
//	,	CreateEncode                  // used when creating a codec entry while encoding
//	,	NoHot                         // dep access is guarded and cannot be hot
//	// aliases
//	,	NRule = CreateEncode          // number of Dflag's allowed in rule definition
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class Tflag : uint8_t { // flags for targets, recorded in server book-keeping
//		Essential                // show when generating user oriented graphs
//	,	Incremental              // reads are allowed (before earliest write if any)
//	,	NoWarning                // warn if target is either uniquified or unlinked and generated by another rule
//	,	Phony                    // accept that target is not generated
//	,	Static                   // is static  , for internal use only, only if also a Target
//	,	Target                   // is a target, for internal use only
//	//
//	// aliases
//	,	NRule = Static           // number of Tflag's allowed in rule definition
//	,	NDyn  = Phony            // number of Tflag's allowed inside flags
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	// not recorded in server book-keeping
//	enum class ExtraTflag : uint8_t { // flags for targets, not recorded in server book-keeping
//		Top
//	,	Ignore
//	,	Optional
//	,	SourceOk                      // ok to overwrite source files
//	,	Allow                         // writing to this target is allowed (for use in clmake.target and ltarget)
//	,	Late                          // target was written for real, not during washing
//	//
//	// aliases
//	,	NRule = Allow                 // number of Tflag's allowed in rule definition
//	} ;
//	// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO
//		Tflags      tflags       = {} ;                                   // if kind>=Target
//		Dflags      dflags       = {} ;                                   // if kind>=Dep
//		ExtraTflags extra_tflags = {} ;                                   // if kind>=Target
//		ExtraDflags extra_dflags = {} ;                                   // if kind>=Dep
//		// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class Comment : uint8_t {
//		None
//	// syscalls
//	,	access
//	,	canonicalize_file_name
//	,	chdir
//	,	chmod
//	,	chroot
//	,	creat                  , creat64
//	,	dlmopen
//	,	dlopen
//	,	execv                  , execvDep
//	,	execve                 , execveDep       , execveat          , execveatDep
//	,	execvp                 , execvpDep
//	,	execvpe                , execvpeDep
//	,	                                           faccessat         , faccessat2
//	,	fchdir
//	,	                                           fchmodat
//	,	fopen                  , fopen64
//	,	freopen                , freopen64
//	,	                                           fstatat           , fstatat64
//	,	                                           futimesat
//	,	getdents               , getdents64
//	,	getdirentries          , getdirentries64
//	,	glob                   , glob64
//	,	la_objopen
//	,	la_objsearch
//	,	link                                     , linkat
//	,	lstat                  , lstat64
//	,	lutimes
//	,	mkdir                                    , mkdirat
//	,	mkostemp               , mkostemp64
//	,	mkostemps              , mkostemps64
//	,	mkstemp                , mkstemp64
//	,	mkstemps               , mkstemps64
//	,	mount
//	,	                                           name_to_handle_at
//	,	                                           newfstatat
//	,	oldlstat
//	,	oldstat
//	,	open                   , open64          , openat            , openat64     , openat2
//	,	open_tree
//	,	opendir
//	,	readdir                , readdir64       , readdir_r         , readdir64_r
//	,	readlink                                 , readlinkat
//	,	realpath
//	,	rename                                   , renameat          , renameat2
//	,	rmdir
//	,	scandir                , scandir64       , scandirat         , scandirat64
//	,	stat                   , stat64
//	,	statx
//	,	symlink                                  , symlinkat
//	,	truncate               , truncate64
//	,	unlink                                   , unlinkat
//	,	utime
//	,	                                           utimensat
//	,	utimes
//	,	                                           __fxstatat        , __fxstatat64
//	,	                                           __lxstat          , __lxstat64
//	,	__open                 , __open64
//	,	__open_2               , __open64_2      , __openat_2        , __openat64_2
//	,	__open64_nocancel
//	,	__open_nocancel
//	,	__readlink__chk                          , __readlinkat_chk
//	,	__realpath_chk
//	,	__xstat                , __xstat64
//	// lmake functions
//	,	Analyzed
//	,	CheckDeps        , CheckTargets // not Chk... as name is seen by user
//	,	ComputedCrcs
//	,	CreateCodec                     // not Creat... as name is seen by user
//	,	Decode
//	,	DepAndTarget
//	,	Depend
//	,	Encode
//	,	EndJob           , EndOverhead
//	,	EnteredNamespace
//	,	Hot
//	,	Kill
//	,	List
//	,	LostServer
//	,	OsInfo
//	,	Panic
//	,	StartInfo
//	,	StartJob         , StartOverhead
//	,	StaticDep        , StaticDepAndTarget
//	,	StaticExec
//	,	StaticMatch
//	,	StaticTarget
//	,	Stderr           , Stdin              , Stdout
//	,	StillAlive
//	,	Timeout
//	,	Target
//	,	Tmp
//	,	Trace
//	,	UnexpectedTarget
//	,	Unstable
//	,	UploadedToCache
//	,	Wash             , Washed
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class CommentExt : uint8_t {
//		Bind
//	,	Dir
//	,	Direct
//	,	Err
//	,	File
//	,	Last
//	,	LdLibraryPath
//	,	Killed
//	,	Link     // not Lnk as name is seen by user
//	,	NoFollow
//	,	Orig
//	,	Overlay
//	,	Proc
//	,	RunPath
//	,	Read
//	,	Reply
//	,	Stat
//	,	Tmp
//	,	Unlink   // not Unlnk as name is seen by user
//	,	Verbose
//	,	Write
//	} ;
//	using CommentExts = BitMap<CommentExt> ;
//	// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO CODEC
//		using CodecCrc = Hash::Crc96 ;                                                                              // 64 bits is enough, but not easy to prove
//		static constexpr char CodecSep    = '*'       ; //!                                                    null
//		static constexpr char DecodeSfx[] = ".decode" ; static constexpr size_t DecodeSfxSz = sizeof(DecodeSfx)-1 ;
//		static constexpr char EncodeSfx[] = ".encode" ; static constexpr size_t EncodeSfxSz = sizeof(EncodeSfx)-1 ;
//		// END_OF_VERSIONING
//				// START_OF_VERSIONING REPO CACHE
//				constexpr size_t CacheLineSz = 64                                                               ; // hint only, defined independently of ::hardware_destructive_interference_size ...
//				constexpr size_t Offset0     = round_up<CacheLineSz>( sizeof(Hdr<Hdr_,Idx,Data>)-sizeof(Data) ) ; // ... to ensure inter-operability
//				// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class FileTag : uint8_t { // FileTag is defined here as it is used for Ddate and disk.hh includes this file anyway
//		None
//	,	Unknown
//	,	Dir
//	,	Lnk
//	,	Reg                        // >=Reg means file is a regular file
//	,	Empty                      // empty and not executable
//	,	Exe                        // a regular file with exec permission
//	//
//	// aliases
//	,	Target = Lnk               // >=Target means file can be generated as a target
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	// PER_FILE_SYNC : add entry here
//	enum class FileSync : uint8_t { // method used to ensure real close-to-open file synchronization (including file creation)
//		Auto
//	,	None
//	,	Dir                         // close file directory after write, open it before read (in practice, open/close upon both events)
//	,	Sync                        // sync file after write
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class ZlvlTag : uint8_t {
//		None
//	,	Zlib
//	,	Zstd
//	// aliases
//	,	Dflt =
//			#if HAS_STD
//				Zstd
//			#elif HAS_ZLIB
//				Zlib
//			#else
//				None
//			#endif
//	} ;
//	// END_OF_VERSIONING

// ********************************************
// * Codec : 603a8fc6deb9a767ed591521309aef40 *
// ********************************************
//
//			// START_OF_VERSIONING CODEC
//				res = AcFd({rfd,node},{.nfs_guard=&nfs_guard}).read() ;                                         // if node exists, it contains the reply
//				// END_OF_VERSIONING
//				// START_OF_VERSIONING CODEC
//				res = read_lnk( {rfd,node} , &nfs_guard ) ;
//				if (+res) {
//					throw_unless( res.ends_with(DecodeSfx) , "bad encode link" ) ;
//					res.resize( res.size() - DecodeSfxSz )                       ;
//				} else {
//					if (_retry_codec(r,crs,node,Comment::Encode)) goto Retry/*BACKWARD*/ ;
//					if ( !crs.is_dir() && !lock ) {
//						lock = {rfd,cf.file} ;
//						lock.lock_shared( cat(host(),'-',::getpid()) ) ;                                                                       // passed id is for debug only
//						goto Retry ;
//					}
//					::string dir_s = CodecFile::s_dir_s(crs.tab) ;
//					creat_store( {rfd,dir_s} , crc_base64 , val , crs.umask , &nfs_guard ) ;                                                   // ensure data exist in store
//					//
//					CodecFile dcf       { false/*encode*/ , crs.tab , ctx , crc_hex.substr(0,min_len) }                                       ;
//					::string& code      = dcf.code()                                                                                          ;
//					::string  ctx_dir_s = dir_name_s(node)                                                                                    ;
//					::string  rel_data  = mk_lcl( cat(dir_s,"store/",substr_view(crc_base64,0,2),'/',substr_view(crc_base64,2)) , ctx_dir_s ) ;
//					// find code
//					for(; code.size()<crc_hex.size() ; code.push_back(crc_hex[code.size()]) ) {
//						::string decode_node = dcf.name() ;
//						try {
//							sym_lnk( {rfd,decode_node} , rel_data       , {.nfs_guard=&nfs_guard,.umask=crs.umask} ) ;
//							sym_lnk( {rfd,node       } , code+DecodeSfx , {.nfs_guard=&nfs_guard,.umask=crs.umask} ) ;                         // create the encode side
//							//
//							FileInfo stamp_fi { dir_s+"stamp" , {.nfs_guard=&nfs_guard} } ;                                                    // stamp created links to logical date to ensure proper ...
//							touch( {rfd,decode_node} , stamp_fi.date , {.nfs_guard=&nfs_guard} ) ;                                             // ... overwritten detection in lmake engine ...
//							touch( {rfd,node       } , stamp_fi.date , {.nfs_guard=&nfs_guard} ) ;                                             // ... if no stamp, date is the epoch, which is fine
//							//
//							if (!crs.is_dir()) {
//								::string new_code = cat(dir_s,"new_codes/",CodecCrc(New,decode_node).base64()) ;
//								sym_lnk( {rfd,new_code} , "../"+node , {.nfs_guard=&nfs_guard} ) ;                                             // tell server
//							}
//							ad.flags.extra_dflags |= ExtraDflag::CreateEncode ;
//							r.report_access( { .comment=Comment::Encode , .digest=ad , .files={{decode_node,FileInfo()}} } , true/*force*/ ) ; // report no access, but with create_encode flag
//							goto Found ;                                                                                                       // if sym_lnk succeeds, we have created the code ...
//						} catch (::string const& e) {                                                                                          // ... (atomicity works even on NFS)
//							::string tgt = read_lnk({rfd,decode_node}) ;
//							if (tgt==rel_data) goto Found ;                                                     // if decode_node already exists with the correct content, ...
//						}                                                                                       // ... it has been created concurrently
//					}
//					throw "no available code"s ;
//				Found :
//					fi  = { {rfd,node} , {.nfs_guard=&nfs_guard} } ;                                            // update date after create
//					res = ::move(code)   ;
//				}
//				// END_OF_VERSIONING
//					// START_OF_VERSIONING REPO CACHE CODEC
//					static constexpr MatchFlags IncPhony { .tflags{Tflag::Incremental,Tflag::Phony,Tflag::Target} } ;
//					stems = {
//						{ "File" ,     ".+"                                            } // static
//					,	{ "Ctx"  , cat("[^",CodecSep,"]*")                             } // star
//					,	{ "Code" ,     "[^/]*"                                         } // .
//					,	{ "Val"  , cat("[A-Za-z0-9_-]{",Codec::CodecCrc::Base64Sz,'}') } // .      /!\ - must be first or last char in []
//					} ;
//					n_static_stems = 1 ;
//					//
//					static ::string pfx = Codec::CodecFile::s_pfx_s() ;
//					job_name = cat(pfx,_stem_mrkr(0/*File*/)) ;
//					matches  = { //!                             File                        Ctx                                                                 File Ctx Code/Val
//						{ "DECODE" , {.pattern=cat(pfx,_stem_mrkr(0 ),'/',CodecSep,_stem_mrkr(1),'/',_stem_mrkr(2/*Code*/),DecodeSfx),.flags=IncPhony,.captures={true,true,true  }} } // star target
//					,	{ "ENCODE" , {.pattern=cat(pfx,_stem_mrkr(0 ),'/',CodecSep,_stem_mrkr(1),'/',_stem_mrkr(3/*Val */),EncodeSfx),.flags=IncPhony,.captures={true,true,true  }} } // .
//					} ;
//					matches_iotas[true/*star*/][+MatchKind::Target] = { 0/*start*/ , VarIdx(matches.size())/*end*/ } ;
//					//
//					deps_attrs.spec.deps = {
//						{ "CODEC_FILE" , {.txt=_stem_mrkr(VarCmd::Stem,0/*File*/),.dflags=DflagsDfltStatic,.extra_dflags=ExtraDflagsDfltStatic} }
//					} ;
//					// END_OF_VERSIONING
//			// START_OF_VERSIONING CODEC
//			::string data = cat(dir_s.file,"store/",substr_view(crc_str,0,2),'/',substr_view(crc_str,2)) ;
//			// END_OF_VERSIONING
//				// START_OF_VERSIONING CODEC
//				AcFd( {dir_s.at,tmp_data} , {.flags=O_WRONLY|O_CREAT,.mod=0444,.umask=umask} ).write( val ) ;
//				// END_OF_VERSIONING
//			// START_OF_VERSIONING CACHE JOB REPO CODEC
//			SWEAR( !is_lcl(node) , node ) ;
//			size_t pos3 = node.rfind('/')        ; SWEAR( pos3!=Npos && 0<pos3              , node,pos3            ) ;
//			size_t pos2 = ext_codec_dir_s.size() ; SWEAR( node.starts_with(ext_codec_dir_s) , node,ext_codec_dir_s ) ;
//			throw_unless( substr_view(node,pos2).starts_with("tab/") , node,"is not a codec file" ) ;
//			//
//			file = node.substr(0,pos2) ;
//			pos3++/* / */ ;
//			if      (node.ends_with(DecodeSfx)) { size_t sz = node.size()-DecodeSfxSz-pos3 ;                                    _code_val_crc = parse_printable<'/'>(node.substr(pos3,sz))    ; }
//			else if (node.ends_with(EncodeSfx)) { size_t sz = node.size()-EncodeSfxSz-pos3 ; SWEAR(sz==CodecCrc::Base64Sz,sz) ; _code_val_crc = CodecCrc::s_from_base64(node.substr(pos3,sz)) ; }
//			else                                  FAIL(node) ;
//			pos3 -= 1/* / */                        ;
//			pos2 += 4/*tab/ */                      ;
//			ctx   = node.substr( pos2 , pos3-pos2 ) ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING CODEC
//			static const ::string DecodeSfxS = with_slash(DecodeSfx) ;
//			static const ::string EncodeSfxS = with_slash(EncodeSfx) ;
//			if (          is_abs     (ctx)                              ) throw cat("context must be a local filename"                         ," : ",ctx," (consider ",ctx.substr(1),')') ;
//			if ( +ctx &&  is_dir_name(ctx)                              ) throw cat("context must not end with /"                              ," : ",ctx," (consider ",ctx,rm_slash ,')') ;
//			if (         !is_lcl     (ctx)                              ) throw cat("context must be a local filename"                         ," : ",ctx                                ) ;
//			if ( ctx.find(DecodeSfxS)!=Npos || ctx.ends_with(DecodeSfx) ) throw cat("context must not contain component ending with ",DecodeSfx," : ",ctx                                ) ;
//			if ( ctx.find(EncodeSfxS)!=Npos || ctx.ends_with(EncodeSfx) ) throw cat("context must not contain component ending with ",EncodeSfx," : ",ctx                                ) ;
//			if ( with_slash(ctx).starts_with(AdminDirS)                 ) throw cat("context must not start with ",no_slash(AdminDirS)         ," : ",ctx                                ) ;
//			if (!is_canon(ctx)) {
//				::string c = mk_canon(ctx) ;
//				if (c==ctx) throw cat("context must be canonical : ",ctx                    ) ;
//				else        throw cat("context must be canonical : ",ctx," (consider ",c,')') ;
//			}
//			// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO CODEC
//		::string CodecFile::ctx_dir_s(bool tmp) const {
//			::string res = s_dir_s(file,tmp) ;
//			if (is_dir_name(file)) res << "tab/"  <<                       ctx  ;
//			else                   res << CodecSep<<mk_printable<CodecSep>(ctx) ;
//			/**/                   res << '/'                                   ;
//			return res ;
//		}
//		::string CodecFile::name(bool tmp) const {
//			::string res = ctx_dir_s(tmp) ;
//			if (is_encode()) res << val_crc().base64()       <<EncodeSfx ;
//			else             res << mk_printable<'/'>(code())<<DecodeSfx ;
//			return res ;
//		}
//		// END_OF_VERSIONING
//			// START_OF_VERSIONING CODEC
//			size_t pos = 0 ;
//			/**/                               throw_unless( line[pos]=='\t' , "bad codec line format : ",line ) ; pos++ ;
//			code = parse_printable(line,pos) ; throw_unless( line[pos]=='\t' , "bad codec line format : ",line ) ; pos++ ;
//			ctx  = parse_printable(line,pos) ; throw_unless( line[pos]=='\t' , "bad codec line format : ",line ) ; pos++ ;
//			val  = parse_printable(line,pos) ; throw_unless( line[pos]==0    , "bad codec line format : ",line ) ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING CODEC
//			::string res = cat('\t',mk_printable(code),'\t',mk_printable(ctx),'\t',mk_printable(val)) ;
//			if (with_nl_) add_nl(res) ;
//			return res ;
//			// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO CODEC
//		using CodecCrc = Hash::Crc96 ;                                                                              // 64 bits is enough, but not easy to prove
//		static constexpr char CodecSep    = '*'       ; //!                                                    null
//		static constexpr char DecodeSfx[] = ".decode" ; static constexpr size_t DecodeSfxSz = sizeof(DecodeSfx)-1 ;
//		static constexpr char EncodeSfx[] = ".encode" ; static constexpr size_t EncodeSfxSz = sizeof(EncodeSfx)-1 ;
//		// END_OF_VERSIONING

// *******************************************
// * Repo : dd9f62e349f7b59a7a5c56915d89970d *
// *******************************************
//
//	// START_OF_VERSIONING CACHE REPO JOB
//		res << ':' ;
//		if (auto_mkdir     ) res << 'm' ;
//		if (deps_in_system ) res << 'X' ;
//		if (disabled       ) res << 'd' ;
//		if (ignore_stat    ) res << 'i' ;
//		if (mount_chroot_ok) res << 'M' ;
//		if (readdir_ok     ) res << 'D' ;
//		switch (file_sync) {
//			case FileSync::Auto : res << "sa" ; break ;
//			case FileSync::None : res << "sn" ; break ;
//			case FileSync::Dir  : res << "sd" ; break ;
//			case FileSync::Sync : res << "ss" ; break ;
//		DF} //! NO_COV
//		switch (lnk_support) {
//			case LnkSupport::None : res << "ln" ; break ;
//			case LnkSupport::File : res << "lf" ; break ;
//			case LnkSupport::Full : res << "la" ; break ;
//		DF} //! NO_COV                                                   empty_ok
//		res <<':'<< '"'<<mk_printable<'"'>(                  fqdn               )<<'"' ;
//		res <<':'<< '"'<<mk_printable<'"'>(                  tmp_dir_s          )<<'"' ;
//		res <<':'<< '"'<<mk_printable<'"'>(                  repo_root_s        )<<'"' ;
//		res <<':'<< '"'<<mk_printable<'"'>(                  sub_repo_s         )<<'"' ;
//		res <<':'<<      mk_printable     (                  src_dirs_s  ,false )      ;
//		res <<':'<<      mk_printable     (mk_vmap<::string>(codecs     ),false )      ;
//		res <<':'<<      mk_printable     (                  views_s     ,false )      ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE REPO
//		FileSig::FileSig(FileInfo const& fi) : FileSig{fi.tag()} {
//			switch (fi.tag()) {
//				case FileTag::None    :
//				case FileTag::Unknown :
//				case FileTag::Dir     :                                                                break ;
//				case FileTag::Empty   : _val |= fi.date.val() & msb_msk<Ddate::Tick>(NBits<FileTag>) ; break ; // improve traceability when size is predictible, 8ns granularity is more than enough
//				case FileTag::Lnk     :
//				case FileTag::Reg     :
//				case FileTag::Exe     : {
//					Xxh h ;
//					h    += fi.date                       ;
//					h    += fi.sz                         ;
//					_val |= +h.digest() << NBits<FileTag> ;
//				} break ;
//			DF}                                                                                                // NO_COV
//		}
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO
//		template<uint8_t Sz> _Crc<Sz>::_Crc(::string const& filename) {
//			// use low level operations to ensure no time-of-check-to time-of-use hasards as crc may be computed on moving files
//			self = None ;
//			if ( AcFd fd{filename,{.flags=O_RDONLY|O_NOFOLLOW,.err_ok=true}} ; +fd ) {
//				FileInfo fi { fd } ;
//				switch (fi.tag()) {
//					case FileTag::Empty :
//						self = Empty ;
//					break ;
//					case FileTag::Reg :
//					case FileTag::Exe : {
//						_Xxh<Sz> ctx { fi.tag() }                   ;
//						::string buf ( ::min(DiskBufSz,fi.sz) , 0 ) ;
//						for( size_t sz=fi.sz ;;) {
//							ssize_t cnt = ::read( fd , buf.data() , buf.size() ) ;
//							if      (cnt> 0) ctx += ::string_view(buf.data(),cnt) ;
//							else if (cnt==0) break ;                                // file could change while crc is being computed
//							else switch (errno) {
//								#if EWOULDBLOCK!=EAGAIN
//									case EWOULDBLOCK :
//								#endif
//								case EAGAIN :
//								case EINTR  : continue                                       ;
//								default     : throw "I/O error while reading file "+filename ;
//							}
//							SWEAR( cnt>0 , cnt ) ;
//							if (size_t(cnt)>=sz) break ;
//							sz -= cnt ;
//						}
//						self = ctx.digest() ;
//					} break ;
//				DN}
//			} else if ( ::string lnk_target=read_lnk(filename) ; +lnk_target ) {
//				_Xxh<Sz> ctx { FileTag::Lnk } ;
//				ctx += ::string_view( lnk_target.data() , lnk_target.size() ) ;     // no need to compute crc on size as would be the case with ctx += lnk_target
//				self = ctx.digest() ;
//			}
//		}
//		// END_OF_VERSIONING
//					// START_OF_VERSIONING REPO CACHE CODEC
//					static constexpr MatchFlags IncPhony { .tflags{Tflag::Incremental,Tflag::Phony,Tflag::Target} } ;
//					stems = {
//						{ "File" ,     ".+"                                            } // static
//					,	{ "Ctx"  , cat("[^",CodecSep,"]*")                             } // star
//					,	{ "Code" ,     "[^/]*"                                         } // .
//					,	{ "Val"  , cat("[A-Za-z0-9_-]{",Codec::CodecCrc::Base64Sz,'}') } // .      /!\ - must be first or last char in []
//					} ;
//					n_static_stems = 1 ;
//					//
//					static ::string pfx = Codec::CodecFile::s_pfx_s() ;
//					job_name = cat(pfx,_stem_mrkr(0/*File*/)) ;
//					matches  = { //!                             File                        Ctx                                                                 File Ctx Code/Val
//						{ "DECODE" , {.pattern=cat(pfx,_stem_mrkr(0 ),'/',CodecSep,_stem_mrkr(1),'/',_stem_mrkr(2/*Code*/),DecodeSfx),.flags=IncPhony,.captures={true,true,true  }} } // star target
//					,	{ "ENCODE" , {.pattern=cat(pfx,_stem_mrkr(0 ),'/',CodecSep,_stem_mrkr(1),'/',_stem_mrkr(3/*Val */),EncodeSfx),.flags=IncPhony,.captures={true,true,true  }} } // .
//					} ;
//					matches_iotas[true/*star*/][+MatchKind::Target] = { 0/*start*/ , VarIdx(matches.size())/*end*/ } ;
//					//
//					deps_attrs.spec.deps = {
//						{ "CODEC_FILE" , {.txt=_stem_mrkr(VarCmd::Stem,0/*File*/),.dflags=DflagsDfltStatic,.extra_dflags=ExtraDflagsDfltStatic} }
//					} ;
//					// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//			::vmap_s<bool> targets ;
//			for( bool star : {false,true} )
//				for( VarIdx mi : matches_iotas[star][+MatchKind::Target] ) // targets (static and star) must be kept first in matches so RuleTgt is stable when match_crc is stable
//					targets.emplace_back( matches[mi].second.pattern , matches[mi].second.flags.extra_tflags[ExtraTflag::Optional] ) ; // keys and flags have no influence on matching, except Optional
//			h += special ;                                                                                                             // in addition to distinguishing special from other, ...
//			h += stems   ;                                                                                                             // ... this guarantees that shared rules have different crc's
//			h += targets ;
//			deps_attrs.update_hash( /*inout*/h , rules ) ;                                                                             // no deps for source & anti
//			if (is_plain()) h += job_name  ;
//			else            h += allow_ext ; // only exists for special rules
//			Crc match_crc = h.digest() ;
//			//
//			if (!is_plain()) {               // no cmd nor resources for special rules
//				crc = {match_crc} ;
//				return ;
//			}
//			h += g_config->lnk_support  ;    // this has an influence on generated deps, hence is part of cmd def
//			h += g_config->os_info      ;    // this has an influence on job execution , hence is part of cmd def
//			h += sub_repo_s             ;
//			h += Node::s_src_dirs_crc() ;    // src_dirs influences deps recording
//			h += matches                ;    // these define names and influence cmd execution, all is not necessary but simpler to code
//			h += force                  ;
//			h += is_python              ;
//			start_cmd_attrs.update_hash( /*inout*/h , rules ) ;
//			cmd            .update_hash( /*inout*/h , rules ) ;
//			Crc cmd_crc = h.digest() ;
//			//
//			submit_rsrcs_attrs.update_hash( /*inout*/h , rules ) ;
//			start_rsrcs_attrs .update_hash( /*inout*/h , rules ) ;
//			Crc rsrcs_crc = h.digest() ;
//			//
//			crc = { match_crc , cmd_crc , rsrcs_crc } ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//			::string dir_s = g_config->local_admin_dir_s+"store/" ;
//			//
//			_g_rules_filename = dir_s+"rule" ;
//			// jobs
//			_g_job_file      .init( dir_s+"job"       , g_writable ) ;
//			_g_job_name_file .init( dir_s+"job_name"  , g_writable ) ;
//			_g_deps_file     .init( dir_s+"deps"      , g_writable ) ;
//			_g_targets_file  .init( dir_s+"targets"   , g_writable ) ;
//			// nodes
//			_g_node_file     .init( dir_s+"node"      , g_writable ) ;
//			_g_node_name_file.init( dir_s+"node_name" , g_writable ) ;
//			_g_job_tgts_file .init( dir_s+"job_tgts"  , g_writable ) ;
//			// rules
//			_g_rule_crc_file .init( dir_s+"rule_crc"  , g_writable ) ; if ( g_writable && !_g_rule_crc_file.c_hdr() ) _g_rule_crc_file.hdr() = 1 ; // hdr is match_gen, 0 is reserved to mean no match
//			_g_rule_tgts_file.init( dir_s+"rule_tgts" , g_writable ) ;
//			_g_sfxs_file     .init( dir_s+"sfxs"      , g_writable ) ;
//			_g_pfxs_file     .init( dir_s+"pfxs"      , g_writable ) ;
//			// misc
//			if (g_writable) {
//				g_seq_id = &_g_job_file.hdr().seq_id ;
//				if (!*g_seq_id) *g_seq_id = 1 ;                // avoid 0 (when store is brand new) to decrease possible confusion
//			}
//			// Rule
//			RuleBase::s_match_gen = _g_rule_crc_file.c_hdr() ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING CACHE JOB REPO
//			SWEAR( is_lcl(node) , node ) ;
//			size_t pos1 = s_pfx_s().size()            ;
//			size_t pos3 = node.rfind('/'            ) ; SWEAR( pos3!=Npos && pos1<pos3                      , node,pos1,     pos3 ) ;
//			size_t pos2 = node.rfind(CodecSep,pos3-1) ; SWEAR( pos2!=Npos && pos1<pos2 && node[pos2-1]=='/' , node,pos1,pos2,pos3 ) ;
//			//
//			file = node.substr(pos1,pos2-pos1) ; file.pop_back() ;
//			pos3++/* / */ ;
//			if      (node.ends_with(DecodeSfx)) { size_t sz = node.size()-DecodeSfxSz-pos3 ;                                    _code_val_crc = parse_printable<'/'>(node.substr(pos3,sz))    ; }
//			else if (node.ends_with(EncodeSfx)) { size_t sz = node.size()-EncodeSfxSz-pos3 ; SWEAR(sz==CodecCrc::Base64Sz,sz) ; _code_val_crc = CodecCrc::s_from_base64(node.substr(pos3,sz)) ; }
//			else                                  FAIL(node) ;
//			pos2++/*CodecSep*/ ;
//			ctx = parse_printable<CodecSep>( node.substr( pos2 , pos3-1/* / */-pos2 ) ) ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING CACHE JOB REPO CODEC
//			SWEAR( !is_lcl(node) , node ) ;
//			size_t pos3 = node.rfind('/')        ; SWEAR( pos3!=Npos && 0<pos3              , node,pos3            ) ;
//			size_t pos2 = ext_codec_dir_s.size() ; SWEAR( node.starts_with(ext_codec_dir_s) , node,ext_codec_dir_s ) ;
//			throw_unless( substr_view(node,pos2).starts_with("tab/") , node,"is not a codec file" ) ;
//			//
//			file = node.substr(0,pos2) ;
//			pos3++/* / */ ;
//			if      (node.ends_with(DecodeSfx)) { size_t sz = node.size()-DecodeSfxSz-pos3 ;                                    _code_val_crc = parse_printable<'/'>(node.substr(pos3,sz))    ; }
//			else if (node.ends_with(EncodeSfx)) { size_t sz = node.size()-EncodeSfxSz-pos3 ; SWEAR(sz==CodecCrc::Base64Sz,sz) ; _code_val_crc = CodecCrc::s_from_base64(node.substr(pos3,sz)) ; }
//			else                                  FAIL(node) ;
//			pos3 -= 1/* / */                        ;
//			pos2 += 4/*tab/ */                      ;
//			ctx   = node.substr( pos2 , pos3-pos2 ) ;
//			// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO CODEC
//		::string CodecFile::ctx_dir_s(bool tmp) const {
//			::string res = s_dir_s(file,tmp) ;
//			if (is_dir_name(file)) res << "tab/"  <<                       ctx  ;
//			else                   res << CodecSep<<mk_printable<CodecSep>(ctx) ;
//			/**/                   res << '/'                                   ;
//			return res ;
//		}
//		::string CodecFile::name(bool tmp) const {
//			::string res = ctx_dir_s(tmp) ;
//			if (is_encode()) res << val_crc().base64()       <<EncodeSfx ;
//			else             res << mk_printable<'/'>(code())<<DecodeSfx ;
//			return res ;
//		}
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE REPO JOB
//		bool                             auto_mkdir       = false ;                   // if true  <=> auto mkdir in case of chdir
//		bool                             deps_in_system   = false ;                   // if false <=> system files are simple and considered as deps
//		bool                             disabled         = false ;                   // if false <=> no automatic report
//		bool                             ignore_stat      = false ;                   // if true  <=> stat-like syscalls do not trigger dependencies
//		bool                             mount_chroot_ok  = false ;
//		bool                             readdir_ok       = false ;                   // if true  <=> allow reading local non-ignored dirs
//		::string                         fast_report_pipe ;                           // pipe to report accesses, faster than sockets, but does not allow replies
//		KeyedService                     service          ;
//		::string                         sub_repo_s       ;                           // relative to repo_root_s
//		::umap_s<Codec::CodecRemoteSide> codecs           ;
//		::vmap_s<::vector_s>             views_s          ;
//		// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class Access : uint8_t {                                                         // in all cases, dirs are deemed non-existing
//		Lnk                                                                               // file is accessed with readlink              , regular files are deemed non-existing
//	,	Reg                                                                               // file is accessed with open                  , symlinks      are deemed non-existing
//	,	Stat                                                                              // file is sensed for existence only
//	,	Err                                                                               // dep is sensitive to status (ok/err)
//	//
//	// aliases
//	,	Data = Err                                                                        // <= Data means refer to file content
//	} ;
//	// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//			::vmap_s<Codec::CodecServerSide> codecs                 ;
//			::string                         key                    ;                    // random key to differentiate repo from other repos
//			LnkSupport                       lnk_support            = LnkSupport::Full ;
//			::string                         os_info                ;                    // os version/release/architecture
//			::string                         user_local_admin_dir_s ;
//			// END_OF_VERSIONING
//				// START_OF_VERSIONING REPO
//				size_t   sz       = 100<<20      ;
//				Channels channels = DfltChannels ;
//				JobIdx   n_jobs   = 1000         ;
//				// END_OF_VERSIONING
//				// START_OF_VERSIONING REPO
//				::serdes( s , caches                                            ) ;
//				::serdes( s , ddate_prec,heartbeat,heartbeat_tick,network_delay ) ;
//				::serdes( s , extra_manifest                                    ) ;
//				::serdes( s , max_dep_depth,path_max                            ) ;
//				::serdes( s , rules_action,srcs_action                          ) ;
//				::serdes( s , sub_repos_s                                       ) ;
//				::serdes( s , system_tag                                        ) ;
//				::serdes( s , trace                                             ) ;
//				// END_OF_VERSIONING REPO
//			// START_OF_VERSIONING REPO
//			::vmap_s<::vmap_ss> caches         ;
//			Time::Delay         ddate_prec     { 0.01 } ; // precision of dates on disk
//			::vector_s          extra_manifest ;
//			Time::Delay         heartbeat      { 10   } ; // min time between successive heartbeat probes for any given job
//			Time::Delay         heartbeat_tick { 0.01 } ; // min time between successive heartbeat probes
//			DepDepth            max_dep_depth  = 100    ; // max dep of the whole flow used to detect infinite recursion
//			Time::Delay         network_delay  { 1    } ;
//			size_t              path_max       = 200    ; // if -1 <=> unlimited
//			::string            rules_action   ;          // action to perform to read independently of config
//			::string            srcs_action    ;          // .
//			::vector_s          sub_repos_s    ;
//			::string            system_tag     ;
//			TraceConfig         trace          ;
//			// END_OF_VERSIONING
//				// START_OF_VERSIONING REPO
//				::string  domain_name ;
//				::vmap_ss dct         ;
//				::vmap_ss env         ;
//				bool      configured  = false ;
//				// END_OF_VERSIONING
//				// START_OF_VERSIONING REPO
//				::vmap_ss          stems         ;
//				::vector<uint32_t> stem_n_marks  ;
//				::vmap_ss          static_ignore ;
//				::vmap_ss          star_ignore   ;
//				// END_OF_VERSIONING
//				// START_OF_VERSIONING REPO
//				uint8_t  date_prec    = 0     ; // -1 means no date at all in console output
//				uint8_t  host_len     = 0     ; //  0 means no host at all in console output
//				uint32_t history_days = 7     ; // number of days during which output log history is kept in LMAKE/outputs, 0 means no log
//				bool     has_exe_time = true  ;
//				bool     show_eta     = false ;
//				bool     show_ete     = true  ;
//				// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//			FileSync                                                                file_sync        = {}  ; // method to ensure file sync when over an unreliable filesystem such as NFS
//			size_t                                                                  max_err_lines    = 0   ; // unlimited
//			uint8_t                                                                 nice             = 0   ; // nice value applied to jobs
//			FileSync                                                                server_file_sync = {}  ; // method to use on server side
//			Collect                                                                 collect          ;
//			Console                                                                 console          ;
//			::array<Backend,N<BackendTag>>                                          backends         ;       // backend may refuse dynamic modification
//			::array<::array<::array<uint8_t,3/*RGB*/>,2/*reverse_video*/>,N<Color>> colors           = {}  ;
//			::map_ss                                                                dbg_tab          = {}  ; // maps debug keys to modules to import, ordered to be serializable
//			// END_OF_VERSIONING
//				// START_OF_VERSIONING REPO
//				::serdes(s,static_cast<ConfigClean &>(self)) ;
//				::serdes(s,static_cast<ConfigStatic&>(self)) ;
//				::serdes(s,static_cast<ConfigDyn   &>(self)) ;
//				// END_OF_VERSIONING
//				// START_OF_VERSIONING REPO
//				::serdes(s,py_sys_path             ) ;    // when deserializing, py_sys_path must be restored before reading RuleData's
//				::serdes(s,static_cast<Base&>(self)) ;
//				::serdes(s,sys_path_crc            ) ;
//				// cant directly serdes the vector as we need a context for DynEntry's
//				uint32_t sz ;
//				if (IsIStream<S>) {                                 ::serdes(s,sz) ; dyn_vec.resize(sz) ; }
//				else              { sz = uint32_t(dyn_vec.size()) ; ::serdes(s,sz) ;                      }
//				for( DynEntry& de : dyn_vec ) de.serdes(s,this) ;
//				// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class JobInfoKind : uint8_t {
//		None
//	,	Start
//	,	End
//	,	DepCrcs
//	} ;
//	// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO CACHE
//			CacheIdx            cache_idx1   = 0     ; // 0 means no cache
//			::vmap_s<DepDigest> deps         = {}    ;
//			bool                live_out     = false ;
//			uint8_t             nice         = -1    ; // -1 means not specified
//			Time::CoarseDelay   pressure     = {}    ;
//			JobReason           reason       = {}    ;
//			Tokens1             tokens1      = 0     ;
//			BackendTag          used_backend = {}    ; // tag actually used (possibly made local because asked tag is not available)
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO CACHE
//			Hash::Crc        rule_crc_cmd = {} ;
//			::vector_s       stems        = {} ;
//			Time::Pdate      eta          = {} ;
//			SubmitInfo       submit_info  = {} ;
//			::vmap_ss        rsrcs        = {} ;
//			JobStartRpcReq   pre_start    = {} ;
//			JobStartRpcReply start        = {} ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO CACHE
//			JobInfoStart                            start    ;
//			JobEndRpcReq                            end      ;
//			::vector<::pair<Hash::Crc,bool/*err*/>> dep_crcs ; // optional, if not provided in end.digest.deps
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//			struct IfPlain {
//				Node        asking   ;                                    //     32 bits,        last target needing this job
//				Targets     targets  ;                                    //     32 bits, owned, for plain jobs
//				CoarseDelay exe_time ;                                    //     16 bits,        for plain jobs
//				CoarseDelay cost     ;                                    //     16 bits,        exe_time / average number of parallel jobs during execution, /!\ must be stable during job execution
//			} ;
//			struct IfDep {
//				SeqId seq_id     = 0 ;                                    //     64 bits
//				Fd    fd         ;                                        //     32 bits
//				Job   asking_job ;                                        //     32 bits
//			} ;
//		public :
//		//	JobName          name                               ;         //     32 bits, inherited
//			Deps             deps                               ;         // 31<=32 bits, owned
//			RuleCrc          rule_crc                           ;         //     32 bits
//			Tokens1          tokens1                            = 0     ; //      8 bits,           for plain jobs, number of tokens - 1 for eta estimation
//			mutable MatchGen match_gen                          = 0     ; //      8 bits,           if <Rule::s_match_gen => deemed !sure
//			RunStatus        run_status    :NBits<RunStatus   > = {}    ; //      3 bits
//			BackendTag       backend       :NBits<BackendTag  > = {}    ; //      2 bits            backend asked for last execution
//			CacheHitInfo     cache_hit_info:NBits<CacheHitInfo> = {}    ; //      3 bits
//			Status           status        :NBits<Status      > = {}    ; //      4 bits
//			bool             incremental   :1                   = false ; //      1 bit ,           job was last run with existing incremental targets
//		private :
//			mutable bool _sure          :1 = false ;                      //      1 bit
//			Bool3        _reliable_stats:2 = No    ;                      //      2 bits,           if No <=> no known info, if Maybe <=> guestimate only, if Yes <=> recorded info
//		public :
//			union {
//				IfPlain  _if_plain = {} ;                                 //     96 bits
//				IfDep    _if_dep   ;                                      //    128 bits
//			} ;
//			// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO
//	enum class Buildable : uint8_t {
//		Anti                         //                                   match independent, include uphill dirs of Src/SrcDir listed in manifest
//	,	SrcDir                       //                                   match independent, SrcDir listed in manifest (much like star targets, i.e. only existing files are deemed buildable)
//	,	SubSrc                       //                                   match independent, sub-file of a Src listed in manifest
//	,	PathTooLong                  //                                   match dependent  , (as limit may change with config)
//	,	DynAnti                      //                                   match dependent
//	,	No                           // <=No means node is not buildable
//	,	Maybe                        //                                   buildability is data dependent (maybe converted to Yes by further analysis)
//	,	SubSrcDir                    //                                   sub-file of a SrcDir
//	,	Unknown
//	,	Yes                          // >=Yes means node is buildable
//	,	Codec                        //                                   match independent, file is a encode or decode marker (LMAKE/lmake/codec/file/ctx/*(.decode|.encode)
//	,	DynSrc                       //                                   match dependent
//	,	Src                          //                                   file listed in manifest, match independent
//	,	Loop                         //                                   node is being analyzed, deemed buildable so as to block further analysis
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO
//	enum class Polluted : uint8_t {
//		Clean                       // must be first
//	,	Old
//	,	PreExist
//	,	Job
//	} ;
//	// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//		public :
//		//	NodeName  name                       ;                      //         32 bits, inherited
//			Watcher   asking                     ;                      //         32 bits,           last watcher needing this node
//			Crc       crc                        = Crc::None          ; // ~45   < 64 bits,           disk file CRC when file mtime was date. 45 bits : MTBF=1000 years @ 1000 files generated per second
//			SigDate   sig                        ;                      // ~40+40<128 bits,           date : production date, sig : if file sig is sig, crc is valid, 40 bits : 30 years @ms resolution
//			Node      dir                        ;                      //  31   < 32 bits, shared
//			JobTgts   job_tgts                   ;                      //         32 bits, owned ,   ordered by prio, valid if match_ok, may contain extra JobTgt's (a reservoir to avoid matching)
//			RuleTgts  rule_tgts                  ;                      // ~20   < 32 bits, shared,   matching rule_tgts issued from suffix on top of job_tgts, valid if match_ok
//			RuleTgts  rejected_rule_tgts         ;                      // ~20   < 32 bits, shared,   rule_tgts known not to match, independent of match_ok
//			Job       actual_job                 ;                      //  31   < 32 bits, shared,   job that generated node
//			Job       polluting_job              ;                      //         32 bits,           polluting job when polluted was last set to Polluted::Job
//			RuleIdx   n_job_tgts                 = 0                  ; //         16 bits,           number of actual meaningful JobTgt's in job_tgts
//			MatchGen  match_gen                  = 0                  ; //          8 bits,           if <Rule::s_match_gen => deem n_job_tgts==0 && !rule_tgts && !sure
//			Buildable buildable:NBits<Buildable> = Buildable::Unknown ; //          4 bits,           data independent, if Maybe => buildability is data dependent, if Plain => not yet computed
//			Polluted  polluted :NBits<Polluted > = Polluted::Clean    ; //          2 bits,           reason for pollution
//			bool      busy     :1                = false              ; //          1 bit ,           a job is running with this node as target
//			Tflags    actual_tflags              ;                      //   6   <  8 bits,           tflags associated with actual_job
//		private :
//			RuleIdx _conform_idx = -+NodeStatus::Unknown ;              //         16 bits,            index to job_tgts to first job with execut.ing.ed prio level, if NoIdx <=> uphill or no job found
//			// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO
//
//	enum class DynImport : uint8_t {
//		Static                       // may import when computing glbs
//	,	Dyn                          // may import when executing code
//	} ;
//
//	enum class DynKind : uint8_t {
//		None
//	,	ShellCmd     // static shell cmd
//	,	PythonCmd    // python cmd (necessarily static)
//	,	Dyn          // dynamic  code, not compiled
//	,	Compiled     // compiled code, glbs not computed
//	,	CompiledGlbs // compiled code, glbs     computed
//	} ;
//
//	enum class EnvFlag : uint8_t {
//		None                       // ignore variable
//	,	Rsrc                       // consider variable as a resource : upon modification, rebuild job if it was in error
//	,	Cmd                        // consider variable as a cmd      : upon modification, rebuild job
//	//
//	// aliases
//	,	Dflt = Rsrc
//	} ;
//
//	enum class RuleCrcState : uint8_t {
//		Ok
//	,	RsrcsOld
//	,	RsrcsForgotten   // when rsrcs are forgotten (and rsrcs were old), process as if cmd changed (i.e. always rerun)
//	,	CmdOld
//	//
//	// aliases
//	,	CmdOk = RsrcsOld // <=CmdOk means no need to run job because of cmd
//	} ;
//
//	enum class Special : uint8_t {
//		None                       // value 0 reserved to mean not initialized
//	,	Dep                        // used for synthetized jobs when asking for direct dep
//	,	Req                        // used for synthetized jobs representing a Req
//	,	InfiniteDep
//	,	InfinitePath
//	,	Codec
//	,	Plain
//	// ordered by decreasing matching priority within each prio
//	,	Anti
//	,	GenericSrc
//	//
//	// aliases
//	,	NUniq      = Plain         // < NUniq      means there is a single such rule
//	,	HasJobs    = Plain         // <=HasJobs    means jobs can refer to this rule
//	,	HasMatches = Codec         // >=HasMatches means rules can get jobs by matching
//	,	HasTargets = InfiniteDep   // >=HasTargets means targets field exists
//	} ;
//	inline bool is_infinite(Special s) { return s==Special::InfiniteDep || s==Special::InfinitePath ; }
//
//	enum class VarCmd : uint8_t {
//		Stems   , Stem
//	,	Targets , Match , StarMatch
//	,	Deps    , Dep
//	,	Rsrcs   , Rsrc
//	} ;
//
//	// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//			bool dyn_deps = false ;
//			::vmap_s<DepSpec> deps ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//			bool       dyn_rsrcs = false             ;
//			BackendTag backend   = BackendTag::Local ;                                                                                  // backend to use to launch jobs
//			::vmap_ss  rsrcs     ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//			::string cache_name ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//			::string   chroot_dir_s    ;
//			bool       dyn_env         = false ;
//			bool       dyn_views       = false ;
//			bool       auto_mkdir      = false ;
//			::vmap_ss  env             ;
//			bool       ignore_stat     = false ;
//			::vector_s interpreter     ;
//			bool       mount_chroot_ok = false ;
//			bool       stderr_ok       = false ;
//			JobSpace   job_space       ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//			bool          dyn_env        = false               ;
//			bool          chk_abs_paths  = false               ;
//			ChrootActions chroot_actions ;
//			::vmap_ss     env            ;
//			::string      lmake_root_s   ;
//			AutodepMethod method         = AutodepMethod::Dflt ;
//			bool          readdir_ok     = false               ;
//			Time::Delay   timeout        ;                                                                    // if 0 <=> no timeout, maximum time allocated to job execution in s
//			bool          use_script     = false               ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//			bool              dyn_env        = false ;
//			::vmap_ss         env            ;
//			bool              keep_tmp       = false ;
//			bool              kill_daemons   = false ;
//			::vector<uint8_t> kill_sigs      ;                                                                 // signals to use to kill job (tried in sequence, 1s apart from each other)
//			uint16_t          max_stderr_len = 0     ;                                                         // max lines when displaying stderr, 0 means no limit (full content is shown with lshow -e)
//			Time::Delay       start_delay    ;                                                                 // job duration above which a start message is generated
//			Zlvl              zlvl           = {}    ;
//			// END_OF_VERSIONING
//				// START_OF_VERSIONING REPO
//				/**/             h += spec        ;
//				/**/             h += has_entry() ;
//				if (has_entry()) const_cast<DynEntry&>(entry(rs)).serdes( /*inout*/h , &rs ) ; // serdes is declared non-const because it is also used for deserializing
//				// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//			using Prio = double ;
//			// END_OF_VERSIONING
//				// START_OF_VERSIONING REPO
//				::string       pattern  = {} ;
//				MatchFlags     flags    = {} ;
//				::vector<bool> captures = {} ;                           // indexed by stem, true if stem is referenced
//				// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//			// user data
//		public :
//			Special              special    = Special::None ;
//			Prio                 user_prio  = 0             ;                          // the priority of the rule as specified by user
//			RuleIdx              prio       = 0             ;                          // the relative priority of the rule
//			::string             name       ;                                          // the short message associated with the rule
//			::vmap_ss            stems      ;                                          // stems are ordered : statics then stars, stems used as both static and star appear twice
//			::string             sub_repo_s ;                                          // sub_repo which this rule belongs to
//			::string             job_name   ;                                          // used to show in user messages (not all fields are actually used)
//			::vmap_s<MatchEntry> matches    ;                                          // keep user within each star/MatchKind sequence, targets (static and star) are first to ensure RuleTgt stability
//			VarIdx               stdout_idx = NoVar         ;                          // index of target used as stdout
//			VarIdx               stdin_idx  = NoVar         ;                          // index of dep used as stdin
//			bool                 allow_ext  = false         ;                          // if true <=> rule may match outside repo
//			DynDepsAttrs         deps_attrs ;                                          // in match crc, evaluated at job creation time
//			// following is only if plain rules
//			Dyn<SubmitRsrcsAttrs    > submit_rsrcs_attrs     ;                         // in rsrcs crc, evaluated at submit time
//			Dyn<SubmitAncillaryAttrs> submit_ancillary_attrs ;                         // in no    crc, evaluated at submit time
//			DynStartCmdAttrs          start_cmd_attrs        ;                         // in cmd   crc, evaluated before execution
//			Dyn<StartRsrcsAttrs     > start_rsrcs_attrs      ;                         // in rsrcs crc, evaluated before execution
//			Dyn<StartAncillaryAttrs > start_ancillary_attrs  ;                         // in no    crc, evaluated before execution
//			DynCmd                    cmd                    ;                         // in cmd   crc, evaluated before execution
//			bool                      is_python              = false ;
//			bool                      force                  = false ;
//			uint8_t                   n_losts                = 0     ;                 // max number of times a job can be lost
//			uint16_t                  n_runs                 = 0     ;                 // max number of times a job can be run                               , 0 = infinity
//			uint16_t                  n_submits              = 0     ;                 // max number of times a job can be submitted (except losts & retries), 0 = infinity
//			// derived data
//			::vector<uint32_t> stem_n_marks                           ;                // number of capturing groups within each stem
//			RuleCrc            crc                                    ;
//			VarIdx             n_static_stems                         = 0  ;
//			Iota2<VarIdx>      matches_iotas[2/*star*/][N<MatchKind>] = {} ;           // range in matches for each kind of match
//			// stats
//			mutable Delay    cost_per_token = {} ;                                     // average cost per token
//			mutable Delay    exe_time       = {} ;                                     // average exe_time
//			mutable uint64_t tokens1_32     = 0  ; static_assert(sizeof(Tokens1)<=4) ; // average number of tokens1 <<32
//			mutable JobIdx   stats_weight   = 0  ;                                     // number of jobs used to compute average cost_per_token and exe_time
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//			Crc   match ;
//			Crc   cmd   ;
//			Crc   rsrcs ;
//			Rule  rule  = {}            ; // rule associated with match
//			State state = State::CmdOld ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO
//			Kind     kind_ ;
//			::string buf   ;
//			if constexpr (IsHash) {
//				kind_ = ::min(Kind::Dyn,kind) ;                                                                                               // marshal is unstable and cannot be used for hash computation
//			} else if (!IsIStream<S>) {
//				kind_ = kind ;
//				if (kind_==Kind::CompiledGlbs)
//					try                     { buf   = serialize(glbs) ; }
//					catch (::string const&) { kind_ = Kind::Compiled  ; }
//			}
//			::serdes(s,kind_) ;
//			if (IsIStream<S>) {
//				kind = kind_ ;
//			}
//			::serdes(s,ctx ) ;
//			switch (kind_) {
//				case Kind::None         :                                                                                             break ;
//				case Kind::ShellCmd     : ::serdes( s , code_str                          ) ;                                         break ;
//				case Kind::PythonCmd    : ::serdes( s , code_str , glbs_str               ) ; { if (!IsHash) ::serdes(s,dbg_info) ; } break ; // dbg_info is not hashed as it no semantic value
//				case Kind::Dyn          : ::serdes( s , code_str , glbs_str  , may_import ) ; { if (!IsHash) ::serdes(s,dbg_info) ; } break ;
//				case Kind::Compiled     : ::serdes( s , code     , glbs_code , may_import ) ;                                       ; break ;
//				case Kind::CompiledGlbs : ::serdes( s , code     , buf       , may_import ) ;                                       ; break ; // buf is marshaled info
//			}
//			if constexpr (IsHash) {
//				if ( kind_>=Kind::Dyn && +may_import ) { SWEAR(rs) ; ::serdes(s,rs->sys_path_crc) ; }
//			} else if (IsIStream<S>) {
//				if      (kind_==Kind::Compiled    ) { SWEAR(rs) ; glbs = glbs_code->run( nullptr/*glbs*/ , rs->py_sys_path ) ; kind = Kind::CompiledGlbs ; }
//				else if (kind_==Kind::CompiledGlbs) { deserialize(buf,glbs) ;                                                                              }
//			}
//			// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO
//		template<IsStream S> void RuleData::serdes(S& s) {
//			::serdes(s,special   ) ;
//			::serdes(s,user_prio ) ;
//			::serdes(s,prio      ) ;
//			::serdes(s,name      ) ;
//			::serdes(s,stems     ) ;
//			::serdes(s,sub_repo_s) ;
//			::serdes(s,job_name  ) ;
//			::serdes(s,matches   ) ;
//			::serdes(s,stdout_idx) ;
//			::serdes(s,stdin_idx ) ;
//			::serdes(s,allow_ext ) ;
//			::serdes(s,deps_attrs) ;
//			::serdes(s,force     ) ;
//			::serdes(s,n_losts   ) ;
//			::serdes(s,n_runs    ) ;
//			::serdes(s,n_submits ) ;
//			if (special==Special::Plain) {
//				::serdes(s,submit_rsrcs_attrs    ) ;
//				::serdes(s,submit_ancillary_attrs) ;
//				::serdes(s,start_cmd_attrs       ) ;
//				::serdes(s,start_rsrcs_attrs     ) ;
//				::serdes(s,start_ancillary_attrs ) ;
//				::serdes(s,cmd                   ) ;
//				::serdes(s,is_python             ) ;
//				// stats
//				::serdes(s,cost_per_token        ) ;
//				::serdes(s,exe_time              ) ;
//				::serdes(s,stats_weight          ) ;
//			}
//			// derived
//			::serdes(s,stem_n_marks  ) ;
//			::serdes(s,crc           ) ;
//			::serdes(s,n_static_stems) ;
//			::serdes(s,matches_iotas ) ;
//		}
//		inline Disk::FileNameIdx RuleData::job_sfx_len() const {
//			return
//				1                                            // JobMrkr to disambiguate w/ Node names
//			+	n_static_stems * sizeof(Disk::FileNameIdx)*2 // pos+len for each stem
//			+	sizeof(Crc::Val)                             // Rule
//			;
//		}
//		inline ::string RuleData::job_sfx() const {
//			::string res( job_sfx_len() , JobMrkr ) ;
//			encode_int( &res[res.size()-sizeof(Crc::Val)] , +crc->match ) ;
//			return res ;
//		}
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO
//
//		struct JobHdr {
//			SeqId   seq_id  ;
//			JobTgts frozens ; // these jobs are not rebuilt
//		} ;
//
//		struct NodeHdr {
//			Targets srcs        ;
//			Targets src_dirs    ;
//			Targets frozens     ; // these nodes are not updated, like sources
//			Targets no_triggers ; // these nodes do not trigger rebuild
//		} ;
//
//		//                                          ThreadKey header     index             n_index_bits       key       data          misc
//		// jobs
//		using JobFile      = Store::AllocFile       < 0     , JobHdr   , Job             , NJobIdxBits      ,           JobData                        > ;
//		using JobNameFile  = Store::SinglePrefixFile< 0     , void     , JobName         , NJobNameIdxBits  , char    , Job                            > ;
//		using DepsFile     = Store::VectorFile      < '='   , void     , Deps            , NDepsIdxBits     ,           GenericDep  , NodeIdx , 4      > ; // Deps are compressed when Crc==None
//		using TargetsFile  = Store::VectorFile      < '='   , void     , Targets         , NTargetsIdxBits  ,           Target                         > ;
//		// nodes
//		using NodeFile     = Store::StructFile      < 0     , NodeHdr  , Node            , NNodeIdxBits     ,           NodeData                       > ;
//		using NodeNameFile = Store::SinglePrefixFile< 0     , void     , NodeName        , NNodeNameIdxBits , char    , Node                           > ;
//		using JobTgtsFile  = Store::VectorFile      < '='   , void     , JobTgts::Vector , NJobTgtsIdxBits  ,           JobTgt      , RuleIdx          > ;
//		// rules
//		using RuleCrcFile  = Store::AllocFile       < '='   , MatchGen , RuleCrc         , NRuleCrcIdxBits  ,           RuleCrcData                    > ;
//		using RuleTgtsFile = Store::SinglePrefixFile< '='   , void     , RuleTgts        , NRuleTgtsIdxBits , RuleTgt , void        , true /*Reverse*/ > ;
//		using SfxFile      = Store::SinglePrefixFile< '='   , void     , PsfxIdx         , NPsfxIdxBits     , char    , PsfxIdx     , true /*Reverse*/ > ; // map sfxes to root of pfxes
//		using PfxFile      = Store::MultiPrefixFile < '='   , void     , PsfxIdx         , NPsfxIdxBits     , char    , RuleTgts    , false/*Reverse*/ > ;
//
//		static constexpr char StartMrkr = 0x0 ; // used to indicate a single match suffix (i.e. a suffix which actually is an entire filename)
//
//		// END_OF_VERSIONING
//						// START_OF_VERSIONING REPO
//						::vector_s        keys  ;
//						::vector<uint8_t> bytes ;
//						// END_OF_VERSIONING
//						// START_OF_VERSIONING REPO
//						::serdes(os,keys ) ;
//						::serdes(os,bytes) ;
//						// END_OF_VERSIONING
//						// START_OF_VERSIONING REPO
//						::vector_s        keys  ;
//						::vector<uint8_t> bytes ;
//						::serdes(is,keys ) ;
//						::serdes(is,bytes) ;
//						// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class LnkSupport : uint8_t {
//		None
//	,	File
//	,	Full
//	} ;
//	// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO
//		FileSync   file_sync   = {}               ;
//		LnkSupport lnk_support = LnkSupport::Full ; // by default, be pessimistic
//		::string   repo_root_s = {}               ;
//		::string   tmp_dir_s   = {}               ;
//		::vector_s src_dirs_s  = {}               ;
//		// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//
//	// NXxxBits are used to dimension address space, and hence max number of objects for each category.
//	// can be tailored to fit neeeds
//	static constexpr uint8_t NCacheIdxBits    =  8 ; // used to caches
//	static constexpr uint8_t NCodecIdxBits    = 32 ; // used to store code <-> value associations in lencode/ldecode
//	static constexpr uint8_t NDepsIdxBits     = 32 ; // used to index deps
//	static constexpr uint8_t NJobIdxBits      = 30 ; // 2 guard bits
//	static constexpr uint8_t NJobNameIdxBits  = 32 ; // used to index Job names
//	static constexpr uint8_t NJobTgtsIdxBits  = 32 ; // JobTgts are used to store job candidate for each Node, so this Idx is a little bit larget than NodeIdx
//	static constexpr uint8_t NNodeIdxBits     = 31 ; // 1 guard bit, there are a few targets per job, so this idx is a little bit larger than JobIdx
//	static constexpr uint8_t NNodeNameIdxBits = 32 ; // used to index Node names
//	static constexpr uint8_t NPsfxIdxBits     = 32 ; // each rule appears in a few Psfx slots, so this idx is a little bit larger than ruleTgtsIdx
//	static constexpr uint8_t NReqIdxBits      =  8 ;
//	static constexpr uint8_t NRuleIdxBits     = 16 ;
//	static constexpr uint8_t NRuleCrcIdxBits  = 32 ;
//	static constexpr uint8_t NRuleStrIdxBits  = 32 ; // used to index serialized Rule description
//	static constexpr uint8_t NRuleTgtsIdxBits = 32 ;
//	static constexpr uint8_t NTargetsIdxBits  = 32 ; // used to index targets
//
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//
//	// can be tailored to fit neeeds
//	using VarIdx = uint8_t ; // used to index stems, targets, deps & rsrcs within a Rule
//
//	// ids
//	// can be tailored to fit neeeds
//	using SmallId = uint32_t ; // used to identify running jobs, could be uint16_t if we are sure that there cannot be more than 64k jobs running at once
//	using SeqId   = uint64_t ; // used to distinguish old report when a job is relaunched, may overflow as long as 2 job executions have different values if the 1st is lost
//
//	// type to hold the dep depth used to track dep loops
//	// can be tailored to fit neeeds
//	using DepDepth = uint16_t ;
//
//	// job tokens
//	// can be tailored to fit neeeds
//	using Tokens1 = uint8_t ; // store number of tokens-1 (so tokens can go from 1 to 256)
//
//	// maximum number of rule generation before a Job/Node clean up is necessary
//	// can be tailored to fit neeeds
//	using MatchGen = uint8_t ;
//
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	// PER_AUTODEP_METHOD : add entry here
//	// >=Ld means a lib is pre-loaded (through LD_AUDIT or LD_PRELOAD)
//	// by default, use a compromize between speed an reliability
//	enum class AutodepMethod : uint8_t {
//		None
//	,	Ptrace
//	#if HAS_LD_AUDIT
//		,	LdAudit
//	#endif
//	,	LdPreload
//	,	LdPreloadJemalloc
//	// aliases
//	#if HAS_LD_AUDIT
//		,	Ld   = LdAudit
//		,	Dflt = LdAudit
//	#else
//		,	Ld   = LdPreload
//		,	Dflt = LdPreload
//	#endif
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class BackendTag : uint8_t { // PER_BACKEND : add a tag for each backend
//		Unknown                       // must be first
//	,	Local
//	,	Sge
//	,	Slurm
//	//
//	// aliases
//	,	Dflt   = Local
//	,	Remote = Sge                  // if >=Remote, backend is remote
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class CacheHitInfo : uint8_t {
//		Hit                             // cache hit
//	,	Match                           // cache matches, but not hit (some deps are missing, hence dont know if hit or miss)
//	,	BadDeps
//	,	NoJob
//	,	NoRule
//	,	BadDownload
//	,	NoDownload
//	,	BadCache
//	,	NoCache
//	// aliases
//	,	Miss = BadDeps                  // >=Miss means cache miss
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class ChrootAction : uint8_t {
//		ResolvConf                      // /etc/resolv.conf is copied from native env to chroot'ed env
//	,	UserName                        // user and root and their groups have a name, existing ones are not preserved
//	} ;
//	using ChrootActions = BitMap<ChrootAction> ;
//
//	// START_OF_VERSIONING REPO CACHE
//	enum class FileActionTag : uint8_t {
//		Src                              // file is src, no action
//	,	None                             // same as unlink except expect file not to exist
//	,	Unlink                           // used in ldebug, so it cannot be Unlnk
//	,	UnlinkWarning                    // .
//	,	UnlinkPolluted                   // .
//	,	Uniquify
//	,	Mkdir
//	,	Rmdir
//	//
//	// aliases
//	,	HasFile = Uniquify               // <=HasFile means action acts on file
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class JobMngtProc : uint8_t {
//		None
//	,	ChkDeps
//	,	ChkTargets // used in JobMngtRpcReply to signal a pre-existing target
//	,	DepDirect
//	,	DepVerbose
//	,	LiveOut
//	,	AddLiveOut // report missing live_out info (Req) or tell job_exec to send missing live_out info (Reply)
//	,	Heartbeat
//	,	Kill
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class JobRpcProc : uint8_t {
//		None
//	,	Start
//	,	ReportStart
//	,	GiveUp      // Req (all if 0) was killed and job was not (either because of other Req's or it did not start yet)
//	,	End
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class JobReasonTag : uint8_t {           // see explanations in table below
//		None
//	,	Retry                                     // job is retried in case of error      if asked so by user
//	,	LostRetry                                 // job is retried in case of lost_error if asked so by user
//	//	with reason
//	,	CacheMatch
//	,	OldErr
//	,	Rsrcs
//	,	PollutedTargets
//	,	ChkDeps
//	,	WasIncremental
//	,	Lost
//	,	WasLost
//	,	Force
//	,	Killed
//	,	Cmd
//	,	New
//	//	with node
//	,	BusyTarget
//	,	NoTarget
//	,	OldTarget
//	,	PrevTarget
//	,	PollutedTarget
//	,	ManualTarget
//	,	ClashTarget
//	// with dep
//	,	BusyDep                                   // job is waiting for an unknown dep
//	,	DepOutOfDate
//	,	DepTransient
//	,	DepUnlnked
//	,	DepUnstable
//	//	with error
//	,	DepOverwritten
//	,	DepDangling
//	,	DepErr
//	,	DepMissingRequired                        // this is actually an error
//	// with missing
//	,	DepMissingStatic                          // this prevents the job from being selected
//	//
//	// aliases
//	,	HasNode = BusyTarget                      // if >=HasNode <=> a node is associated
//	,	HasDep  = BusyDep                         // if >=HasDep  <=> a dep  is associated
//	,	Err     = DepOverwritten                  // if >=Err     <=> a dep  is in error
//	,	Missing = DepMissingStatic                // if >=Missing <=> a dep  is missing
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class Status : uint8_t { // result of job execution
//		New                       // job was never run
//	,	EarlyChkDeps              // dep check failed before job actually started
//	,	EarlyErr                  // job was not started because of error
//	,	EarlyLost                 // job was lost before starting     , retry
//	,	EarlyLostErr              // job was lost before starting     , do not retry
//	,	LateLost                  // job was lost after having started, retry
//	,	LateLostErr               // job was lost after having started, do not retry
//	,	Killed                    // job was killed
//	,	ChkDeps                   // dep check failed
//	,	CacheMatch                // cache just reported deps, not result
//	,	BadTarget                 // target was not correctly initialized or simultaneously written by another job
//	,	Ok                        // job execution ended successfully
//	,	RunLoop                   // job needs to be rerun but we have already run       it too many times
//	,	SubmitLoop                // job needs to be rerun but we have already submitted it too many times
//	,	Err                       // job execution ended in error
//	//
//	// aliases
//	,	Early   = EarlyLostErr    // <=Early means output has not been modified
//	,	Async   = Killed          // <=Async means job was interrupted asynchronously
//	,	Garbage = BadTarget       // <=Garbage means job has not run reliably
//	} ;
//	// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		FileActionTag tag    = {} ;
//		Tflags        tflags = {} ;
//		Hash::Crc     crc    = {} ; // expected (else, quarantine)
//		Disk::FileSig sig    = {} ; // .
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		Accesses accesses ;
//		Dflags   dflags   ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		NodeIdx node = 0                  ;
//		Tag     tag  = JobReasonTag::None ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		size_t            mem = 0  ; // in bytes
//		Time::CoarseDelay cpu = {} ;
//		Time::CoarseDelay job = {} ; // elapsed in job
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		using Base = ::variant< Hash::Crc , Disk::FileSig , Disk::FileInfo > ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		uint8_t       sz                        = 0          ;                                        //   8 bits, number of items in chunk following header (semantically before)
//		Dflags        dflags                    = DflagsDflt ;                                        // 7<8 bits
//		Accesses::Val accesses_      :N<Access> = 0          ;                                        //   4 bits
//		Accesses::Val chunk_accesses_:N<Access> = 0          ;                                        //   4 bits
//		bool          parallel       :1         = false      ;                                        //   1 bit , dep is parallel with prev dep
//		bool          is_crc         :1         = true       ;                                        //   1 bit
//		bool          hot            :1         = false      ;                                        //   1 bit , if true <= file date was very close from access date (within date granularity)
//		bool          err            :1         = false      ;                                        //   1 bit , if true <=> dep is in error (useful if IgnoreErr), valid only if is_crc
//		bool          create_encode  :1         = false      ;                                        //   1 bit , if true <=> dep has been created because of encode
//	private :
//		union {
//			Crc     _crc = {} ;                                                                       // ~45<64 bits
//			FileSig _sig ;                                                                            // ~40<64 bits
//		} ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		Tflags        tflags       = {}    ;
//		ExtraTflags   extra_tflags = {}    ;
//		bool          pre_exist    = false ; // if true <=> file was seen as existing while not incremental
//		bool          written      = false ; // if true <=> file was written or unlinked (if crc==None)
//		Crc           crc          = {}    ; // if None <=> file was unlinked, if Unknown => file is idle (not written, not unlinked)
//		Disk::FileSig sig          = {}    ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		CacheUploadKey           upload_key     = {}          ;
//		::vmap<Key,TargetDigest> targets        = {}          ;
//		::vmap<Key,DepDigest   > deps           = {}          ;                                // INVARIANT : sorted in first access order
//		::vector_s               refresh_codecs = {}          ;
//		Time::CoarseDelay        exe_time       = {}          ;
//		Status                   status         = Status::New ;
//		bool                     has_msg_stderr = false       ;                                // if true <= msg or stderr are non-empty in englobing JobEndRpcReq
//		bool                     incremental    = false       ;                                // if true <= job was run with existing incremental targets
//		// END_OF_VERSIONING
//			// START_OF_VERSIONING REPO CACHE
//			::vector_s phys_s  = {} ;                                                              // (upper,lower...)
//			::vector_s copy_up = {} ;                                                              // dirs & files or dirs to create in upper (mkdir or cp <file> from lower...)
//			// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		::string            lmake_view_s = {} ;                                                    // absolute dir under which job sees open-lmake root dir (empty if unused)
//		::string            repo_view_s  = {} ;                                                    // absolute dir under which job sees repo root dir       (empty if unused)
//		::string            tmp_view_s   = {} ;                                                    // absolute dir under which job sees tmp dir             (empty if unused)
//		::vmap_s<ViewDescr> views        = {} ;                                                    // dir_s->descr, relative to sub_repo when _force_create=Maybe, else relative to repo_root
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		SeqId  seq_id = 0 ;
//		JobIdx job    = 0 ;
//		// END_OF_VERSIONING)
//		// START_OF_VERSIONING REPO CACHE
//		KeyedService service ; // where job_exec can be contacted (except addr which is discovered by server from peer_addr
//		::string     msg     ;
//		// END_OF_VERSIONING)
//		// START_OF_VERSIONING REPO CACHE
//		AutodepEnv                              autodep_env      ;
//		CacheRemoteSide                         cache            ;
//		bool                                    chk_abs_paths    = false               ;
//		ChrootInfo                              chroot_info      ;
//		::string                                cmd              ;
//		Time::Delay                             ddate_prec       ;
//		::vmap_s<::pair<DepDigest,ExtraDflags>> deps             ;                       // deps already accessed (always includes static deps), DepDigest does not include extra_dflags, so add them
//		::string                                domain_name      ;
//		::vmap_ss                               env              ;
//		::vector_s                              interpreter      ;                       // actual interpreter used to execute cmd
//		JobSpace                                job_space        ;
//		bool                                    keep_tmp         = false               ;
//		::string                                key              ;                       // key used to uniquely identify repo
//		bool                                    kill_daemons     = false               ;
//		::vector<uint8_t>                       kill_sigs        ;
//		bool                                    live_out         = false               ;
//		AutodepMethod                           method           = AutodepMethod::Dflt ;
//		Time::Delay                             network_delay    ;
//		uint8_t                                 nice             = 0                   ;
//		::string                                phy_lmake_root_s ;
//		::vmap_s<FileAction>                    pre_actions      ;
//		::string                                rule             ;                       // rule name
//		SmallId                                 small_id         = 0                   ;
//		::vmap<Re::Pattern,MatchFlags>          star_matches     ;                       // maps regexprs to flags
//		::vmap_s<MatchFlags>                    static_matches   ;                       // maps individual files to flags
//		bool                                    stderr_ok        = false               ;
//		::string                                stdin            ;
//		::string                                stdout           ;
//		Time::Delay                             timeout          ;
//		bool                                    use_script       = false               ;
//		Zlvl                                    zlvl             {}                    ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING REPO CACHE
//		JobDigest<>              digest        ;
//		::vmap_ss                dyn_env       ; // env variables computed in job_exec
//		Time::Pdate              end_date      ;
//		MsgStderr                msg_stderr    ;
//		::string                 os_info       ;
//		::string                 phy_tmp_dir_s ;
//		JobStats                 stats         ;
//		::string                 stdout        ;
//		Disk::DiskSz             total_sz      = 0 ;
//		Disk::DiskSz             total_z_sz    = 0 ;
//		::vector<UserTraceEntry> user_trace    ;
//		int                      wstatus       = 0 ;
//		// END_OF_VERSIONING)
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class Dflag : uint8_t { // flags for deps, recorded in server book-keeping
//		Critical                 // if modified, ignore following deps
//	,	Essential                // show when generating user oriented graphs
//	,	IgnoreError              // dont propagate error if dep is in error (Error instead of Err because name is visible from user)
//	,	Required                 // dep must be buildable (static deps are always required)
//	,	Static                   // is static dep, for internal use only
//	,	Codec                    // acquired with codec
//	,	Full                     // if false, dep is only necessary to compute resources
//	//
//	// aliases
//	,	NRule = Required         // number of Dflag's allowed in rule definition
//	,	NDyn  = Static           // number of Dflag's allowed in side flags
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class ExtraDflag : uint8_t { // flags for deps, not recorded in server book-keeping
//		Top
//	,	Ignore
//	,	ReaddirOk
//	,	NoStar                        // exclude flags from star patterns (common info for dep and target)
//	,	CreateEncode                  // used when creating a codec entry while encoding
//	,	NoHot                         // dep access is guarded and cannot be hot
//	// aliases
//	,	NRule = CreateEncode          // number of Dflag's allowed in rule definition
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class Tflag : uint8_t { // flags for targets, recorded in server book-keeping
//		Essential                // show when generating user oriented graphs
//	,	Incremental              // reads are allowed (before earliest write if any)
//	,	NoWarning                // warn if target is either uniquified or unlinked and generated by another rule
//	,	Phony                    // accept that target is not generated
//	,	Static                   // is static  , for internal use only, only if also a Target
//	,	Target                   // is a target, for internal use only
//	//
//	// aliases
//	,	NRule = Static           // number of Tflag's allowed in rule definition
//	,	NDyn  = Phony            // number of Tflag's allowed inside flags
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	// not recorded in server book-keeping
//	enum class ExtraTflag : uint8_t { // flags for targets, not recorded in server book-keeping
//		Top
//	,	Ignore
//	,	Optional
//	,	SourceOk                      // ok to overwrite source files
//	,	Allow                         // writing to this target is allowed (for use in clmake.target and ltarget)
//	,	Late                          // target was written for real, not during washing
//	//
//	// aliases
//	,	NRule = Allow                 // number of Tflag's allowed in rule definition
//	} ;
//	// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO
//		Tflags      tflags       = {} ;                                   // if kind>=Target
//		Dflags      dflags       = {} ;                                   // if kind>=Dep
//		ExtraTflags extra_tflags = {} ;                                   // if kind>=Target
//		ExtraDflags extra_dflags = {} ;                                   // if kind>=Dep
//		// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class Comment : uint8_t {
//		None
//	// syscalls
//	,	access
//	,	canonicalize_file_name
//	,	chdir
//	,	chmod
//	,	chroot
//	,	creat                  , creat64
//	,	dlmopen
//	,	dlopen
//	,	execv                  , execvDep
//	,	execve                 , execveDep       , execveat          , execveatDep
//	,	execvp                 , execvpDep
//	,	execvpe                , execvpeDep
//	,	                                           faccessat         , faccessat2
//	,	fchdir
//	,	                                           fchmodat
//	,	fopen                  , fopen64
//	,	freopen                , freopen64
//	,	                                           fstatat           , fstatat64
//	,	                                           futimesat
//	,	getdents               , getdents64
//	,	getdirentries          , getdirentries64
//	,	glob                   , glob64
//	,	la_objopen
//	,	la_objsearch
//	,	link                                     , linkat
//	,	lstat                  , lstat64
//	,	lutimes
//	,	mkdir                                    , mkdirat
//	,	mkostemp               , mkostemp64
//	,	mkostemps              , mkostemps64
//	,	mkstemp                , mkstemp64
//	,	mkstemps               , mkstemps64
//	,	mount
//	,	                                           name_to_handle_at
//	,	                                           newfstatat
//	,	oldlstat
//	,	oldstat
//	,	open                   , open64          , openat            , openat64     , openat2
//	,	open_tree
//	,	opendir
//	,	readdir                , readdir64       , readdir_r         , readdir64_r
//	,	readlink                                 , readlinkat
//	,	realpath
//	,	rename                                   , renameat          , renameat2
//	,	rmdir
//	,	scandir                , scandir64       , scandirat         , scandirat64
//	,	stat                   , stat64
//	,	statx
//	,	symlink                                  , symlinkat
//	,	truncate               , truncate64
//	,	unlink                                   , unlinkat
//	,	utime
//	,	                                           utimensat
//	,	utimes
//	,	                                           __fxstatat        , __fxstatat64
//	,	                                           __lxstat          , __lxstat64
//	,	__open                 , __open64
//	,	__open_2               , __open64_2      , __openat_2        , __openat64_2
//	,	__open64_nocancel
//	,	__open_nocancel
//	,	__readlink__chk                          , __readlinkat_chk
//	,	__realpath_chk
//	,	__xstat                , __xstat64
//	// lmake functions
//	,	Analyzed
//	,	CheckDeps        , CheckTargets // not Chk... as name is seen by user
//	,	ComputedCrcs
//	,	CreateCodec                     // not Creat... as name is seen by user
//	,	Decode
//	,	DepAndTarget
//	,	Depend
//	,	Encode
//	,	EndJob           , EndOverhead
//	,	EnteredNamespace
//	,	Hot
//	,	Kill
//	,	List
//	,	LostServer
//	,	OsInfo
//	,	Panic
//	,	StartInfo
//	,	StartJob         , StartOverhead
//	,	StaticDep        , StaticDepAndTarget
//	,	StaticExec
//	,	StaticMatch
//	,	StaticTarget
//	,	Stderr           , Stdin              , Stdout
//	,	StillAlive
//	,	Timeout
//	,	Target
//	,	Tmp
//	,	Trace
//	,	UnexpectedTarget
//	,	Unstable
//	,	UploadedToCache
//	,	Wash             , Washed
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class CommentExt : uint8_t {
//		Bind
//	,	Dir
//	,	Direct
//	,	Err
//	,	File
//	,	Last
//	,	LdLibraryPath
//	,	Killed
//	,	Link     // not Lnk as name is seen by user
//	,	NoFollow
//	,	Orig
//	,	Overlay
//	,	Proc
//	,	RunPath
//	,	Read
//	,	Reply
//	,	Stat
//	,	Tmp
//	,	Unlink   // not Unlnk as name is seen by user
//	,	Verbose
//	,	Write
//	} ;
//	using CommentExts = BitMap<CommentExt> ;
//	// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO CODEC
//		using CodecCrc = Hash::Crc96 ;                                                                              // 64 bits is enough, but not easy to prove
//		static constexpr char CodecSep    = '*'       ; //!                                                    null
//		static constexpr char DecodeSfx[] = ".decode" ; static constexpr size_t DecodeSfxSz = sizeof(DecodeSfx)-1 ;
//		static constexpr char EncodeSfx[] = ".encode" ; static constexpr size_t EncodeSfxSz = sizeof(EncodeSfx)-1 ;
//		// END_OF_VERSIONING
//				// START_OF_VERSIONING REPO CACHE
//				constexpr size_t CacheLineSz = 64                                                               ; // hint only, defined independently of ::hardware_destructive_interference_size ...
//				constexpr size_t Offset0     = round_up<CacheLineSz>( sizeof(Hdr<Hdr_,Idx,Data>)-sizeof(Data) ) ; // ... to ensure inter-operability
//				// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class FileTag : uint8_t { // FileTag is defined here as it is used for Ddate and disk.hh includes this file anyway
//		None
//	,	Unknown
//	,	Dir
//	,	Lnk
//	,	Reg                        // >=Reg means file is a regular file
//	,	Empty                      // empty and not executable
//	,	Exe                        // a regular file with exec permission
//	//
//	// aliases
//	,	Target = Lnk               // >=Target means file can be generated as a target
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	// PER_FILE_SYNC : add entry here
//	enum class FileSync : uint8_t { // method used to ensure real close-to-open file synchronization (including file creation)
//		Auto
//	,	None
//	,	Dir                         // close file directory after write, open it before read (in practice, open/close upon both events)
//	,	Sync                        // sync file after write
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING REPO CACHE
//	enum class ZlvlTag : uint8_t {
//		None
//	,	Zlib
//	,	Zstd
//	// aliases
//	,	Dflt =
//			#if HAS_STD
//				Zstd
//			#elif HAS_ZLIB
//				Zlib
//			#else
//				None
//			#endif
//	} ;
//	// END_OF_VERSIONING

// ******************************************
// * Job : e9815ab26bf928f163f771390de178e6 *
// ******************************************
//
//	// START_OF_VERSIONING CACHE REPO JOB
//		res << ':' ;
//		if (auto_mkdir     ) res << 'm' ;
//		if (deps_in_system ) res << 'X' ;
//		if (disabled       ) res << 'd' ;
//		if (ignore_stat    ) res << 'i' ;
//		if (mount_chroot_ok) res << 'M' ;
//		if (readdir_ok     ) res << 'D' ;
//		switch (file_sync) {
//			case FileSync::Auto : res << "sa" ; break ;
//			case FileSync::None : res << "sn" ; break ;
//			case FileSync::Dir  : res << "sd" ; break ;
//			case FileSync::Sync : res << "ss" ; break ;
//		DF} //! NO_COV
//		switch (lnk_support) {
//			case LnkSupport::None : res << "ln" ; break ;
//			case LnkSupport::File : res << "lf" ; break ;
//			case LnkSupport::Full : res << "la" ; break ;
//		DF} //! NO_COV                                                   empty_ok
//		res <<':'<< '"'<<mk_printable<'"'>(                  fqdn               )<<'"' ;
//		res <<':'<< '"'<<mk_printable<'"'>(                  tmp_dir_s          )<<'"' ;
//		res <<':'<< '"'<<mk_printable<'"'>(                  repo_root_s        )<<'"' ;
//		res <<':'<< '"'<<mk_printable<'"'>(                  sub_repo_s         )<<'"' ;
//		res <<':'<<      mk_printable     (                  src_dirs_s  ,false )      ;
//		res <<':'<<      mk_printable     (mk_vmap<::string>(codecs     ),false )      ;
//		res <<':'<<      mk_printable     (                  views_s     ,false )      ;
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO
//		template<uint8_t Sz> _Crc<Sz>::_Crc(::string const& filename) {
//			// use low level operations to ensure no time-of-check-to time-of-use hasards as crc may be computed on moving files
//			self = None ;
//			if ( AcFd fd{filename,{.flags=O_RDONLY|O_NOFOLLOW,.err_ok=true}} ; +fd ) {
//				FileInfo fi { fd } ;
//				switch (fi.tag()) {
//					case FileTag::Empty :
//						self = Empty ;
//					break ;
//					case FileTag::Reg :
//					case FileTag::Exe : {
//						_Xxh<Sz> ctx { fi.tag() }                   ;
//						::string buf ( ::min(DiskBufSz,fi.sz) , 0 ) ;
//						for( size_t sz=fi.sz ;;) {
//							ssize_t cnt = ::read( fd , buf.data() , buf.size() ) ;
//							if      (cnt> 0) ctx += ::string_view(buf.data(),cnt) ;
//							else if (cnt==0) break ;                                // file could change while crc is being computed
//							else switch (errno) {
//								#if EWOULDBLOCK!=EAGAIN
//									case EWOULDBLOCK :
//								#endif
//								case EAGAIN :
//								case EINTR  : continue                                       ;
//								default     : throw "I/O error while reading file "+filename ;
//							}
//							SWEAR( cnt>0 , cnt ) ;
//							if (size_t(cnt)>=sz) break ;
//							sz -= cnt ;
//						}
//						self = ctx.digest() ;
//					} break ;
//				DN}
//			} else if ( ::string lnk_target=read_lnk(filename) ; +lnk_target ) {
//				_Xxh<Sz> ctx { FileTag::Lnk } ;
//				ctx += ::string_view( lnk_target.data() , lnk_target.size() ) ;     // no need to compute crc on size as would be the case with ctx += lnk_target
//				self = ctx.digest() ;
//			}
//		}
//		// END_OF_VERSIONING
//			// START_OF_VERSIONING CACHE JOB REPO
//			SWEAR( is_lcl(node) , node ) ;
//			size_t pos1 = s_pfx_s().size()            ;
//			size_t pos3 = node.rfind('/'            ) ; SWEAR( pos3!=Npos && pos1<pos3                      , node,pos1,     pos3 ) ;
//			size_t pos2 = node.rfind(CodecSep,pos3-1) ; SWEAR( pos2!=Npos && pos1<pos2 && node[pos2-1]=='/' , node,pos1,pos2,pos3 ) ;
//			//
//			file = node.substr(pos1,pos2-pos1) ; file.pop_back() ;
//			pos3++/* / */ ;
//			if      (node.ends_with(DecodeSfx)) { size_t sz = node.size()-DecodeSfxSz-pos3 ;                                    _code_val_crc = parse_printable<'/'>(node.substr(pos3,sz))    ; }
//			else if (node.ends_with(EncodeSfx)) { size_t sz = node.size()-EncodeSfxSz-pos3 ; SWEAR(sz==CodecCrc::Base64Sz,sz) ; _code_val_crc = CodecCrc::s_from_base64(node.substr(pos3,sz)) ; }
//			else                                  FAIL(node) ;
//			pos2++/*CodecSep*/ ;
//			ctx = parse_printable<CodecSep>( node.substr( pos2 , pos3-1/* / */-pos2 ) ) ;
//			// END_OF_VERSIONING
//			// START_OF_VERSIONING CACHE JOB REPO CODEC
//			SWEAR( !is_lcl(node) , node ) ;
//			size_t pos3 = node.rfind('/')        ; SWEAR( pos3!=Npos && 0<pos3              , node,pos3            ) ;
//			size_t pos2 = ext_codec_dir_s.size() ; SWEAR( node.starts_with(ext_codec_dir_s) , node,ext_codec_dir_s ) ;
//			throw_unless( substr_view(node,pos2).starts_with("tab/") , node,"is not a codec file" ) ;
//			//
//			file = node.substr(0,pos2) ;
//			pos3++/* / */ ;
//			if      (node.ends_with(DecodeSfx)) { size_t sz = node.size()-DecodeSfxSz-pos3 ;                                    _code_val_crc = parse_printable<'/'>(node.substr(pos3,sz))    ; }
//			else if (node.ends_with(EncodeSfx)) { size_t sz = node.size()-EncodeSfxSz-pos3 ; SWEAR(sz==CodecCrc::Base64Sz,sz) ; _code_val_crc = CodecCrc::s_from_base64(node.substr(pos3,sz)) ; }
//			else                                  FAIL(node) ;
//			pos3 -= 1/* / */                        ;
//			pos2 += 4/*tab/ */                      ;
//			ctx   = node.substr( pos2 , pos3-pos2 ) ;
//			// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO CODEC
//		::string CodecFile::ctx_dir_s(bool tmp) const {
//			::string res = s_dir_s(file,tmp) ;
//			if (is_dir_name(file)) res << "tab/"  <<                       ctx  ;
//			else                   res << CodecSep<<mk_printable<CodecSep>(ctx) ;
//			/**/                   res << '/'                                   ;
//			return res ;
//		}
//		::string CodecFile::name(bool tmp) const {
//			::string res = ctx_dir_s(tmp) ;
//			if (is_encode()) res << val_crc().base64()       <<EncodeSfx ;
//			else             res << mk_printable<'/'>(code())<<DecodeSfx ;
//			return res ;
//		}
//		// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE REPO JOB
//		bool                             auto_mkdir       = false ;                   // if true  <=> auto mkdir in case of chdir
//		bool                             deps_in_system   = false ;                   // if false <=> system files are simple and considered as deps
//		bool                             disabled         = false ;                   // if false <=> no automatic report
//		bool                             ignore_stat      = false ;                   // if true  <=> stat-like syscalls do not trigger dependencies
//		bool                             mount_chroot_ok  = false ;
//		bool                             readdir_ok       = false ;                   // if true  <=> allow reading local non-ignored dirs
//		::string                         fast_report_pipe ;                           // pipe to report accesses, faster than sockets, but does not allow replies
//		KeyedService                     service          ;
//		::string                         sub_repo_s       ;                           // relative to repo_root_s
//		::umap_s<Codec::CodecRemoteSide> codecs           ;
//		::vmap_s<::vector_s>             views_s          ;
//		// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class Access : uint8_t {                                                         // in all cases, dirs are deemed non-existing
//		Lnk                                                                               // file is accessed with readlink              , regular files are deemed non-existing
//	,	Reg                                                                               // file is accessed with open                  , symlinks      are deemed non-existing
//	,	Stat                                                                              // file is sensed for existence only
//	,	Err                                                                               // dep is sensitive to status (ok/err)
//	//
//	// aliases
//	,	Data = Err                                                                        // <= Data means refer to file content
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class LnkSupport : uint8_t {
//		None
//	,	File
//	,	Full
//	} ;
//	// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO
//		FileSync   file_sync   = {}               ;
//		LnkSupport lnk_support = LnkSupport::Full ; // by default, be pessimistic
//		::string   repo_root_s = {}               ;
//		::string   tmp_dir_s   = {}               ;
//		::vector_s src_dirs_s  = {}               ;
//		// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//
//	// NXxxBits are used to dimension address space, and hence max number of objects for each category.
//	// can be tailored to fit neeeds
//	static constexpr uint8_t NCacheIdxBits    =  8 ; // used to caches
//	static constexpr uint8_t NCodecIdxBits    = 32 ; // used to store code <-> value associations in lencode/ldecode
//	static constexpr uint8_t NDepsIdxBits     = 32 ; // used to index deps
//	static constexpr uint8_t NJobIdxBits      = 30 ; // 2 guard bits
//	static constexpr uint8_t NJobNameIdxBits  = 32 ; // used to index Job names
//	static constexpr uint8_t NJobTgtsIdxBits  = 32 ; // JobTgts are used to store job candidate for each Node, so this Idx is a little bit larget than NodeIdx
//	static constexpr uint8_t NNodeIdxBits     = 31 ; // 1 guard bit, there are a few targets per job, so this idx is a little bit larger than JobIdx
//	static constexpr uint8_t NNodeNameIdxBits = 32 ; // used to index Node names
//	static constexpr uint8_t NPsfxIdxBits     = 32 ; // each rule appears in a few Psfx slots, so this idx is a little bit larger than ruleTgtsIdx
//	static constexpr uint8_t NReqIdxBits      =  8 ;
//	static constexpr uint8_t NRuleIdxBits     = 16 ;
//	static constexpr uint8_t NRuleCrcIdxBits  = 32 ;
//	static constexpr uint8_t NRuleStrIdxBits  = 32 ; // used to index serialized Rule description
//	static constexpr uint8_t NRuleTgtsIdxBits = 32 ;
//	static constexpr uint8_t NTargetsIdxBits  = 32 ; // used to index targets
//
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//
//	// can be tailored to fit neeeds
//	using VarIdx = uint8_t ; // used to index stems, targets, deps & rsrcs within a Rule
//
//	// ids
//	// can be tailored to fit neeeds
//	using SmallId = uint32_t ; // used to identify running jobs, could be uint16_t if we are sure that there cannot be more than 64k jobs running at once
//	using SeqId   = uint64_t ; // used to distinguish old report when a job is relaunched, may overflow as long as 2 job executions have different values if the 1st is lost
//
//	// type to hold the dep depth used to track dep loops
//	// can be tailored to fit neeeds
//	using DepDepth = uint16_t ;
//
//	// job tokens
//	// can be tailored to fit neeeds
//	using Tokens1 = uint8_t ; // store number of tokens-1 (so tokens can go from 1 to 256)
//
//	// maximum number of rule generation before a Job/Node clean up is necessary
//	// can be tailored to fit neeeds
//	using MatchGen = uint8_t ;
//
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class Dflag : uint8_t { // flags for deps, recorded in server book-keeping
//		Critical                 // if modified, ignore following deps
//	,	Essential                // show when generating user oriented graphs
//	,	IgnoreError              // dont propagate error if dep is in error (Error instead of Err because name is visible from user)
//	,	Required                 // dep must be buildable (static deps are always required)
//	,	Static                   // is static dep, for internal use only
//	,	Codec                    // acquired with codec
//	,	Full                     // if false, dep is only necessary to compute resources
//	//
//	// aliases
//	,	NRule = Required         // number of Dflag's allowed in rule definition
//	,	NDyn  = Static           // number of Dflag's allowed in side flags
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class ExtraDflag : uint8_t { // flags for deps, not recorded in server book-keeping
//		Top
//	,	Ignore
//	,	ReaddirOk
//	,	NoStar                        // exclude flags from star patterns (common info for dep and target)
//	,	CreateEncode                  // used when creating a codec entry while encoding
//	,	NoHot                         // dep access is guarded and cannot be hot
//	// aliases
//	,	NRule = CreateEncode          // number of Dflag's allowed in rule definition
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class Tflag : uint8_t { // flags for targets, recorded in server book-keeping
//		Essential                // show when generating user oriented graphs
//	,	Incremental              // reads are allowed (before earliest write if any)
//	,	NoWarning                // warn if target is either uniquified or unlinked and generated by another rule
//	,	Phony                    // accept that target is not generated
//	,	Static                   // is static  , for internal use only, only if also a Target
//	,	Target                   // is a target, for internal use only
//	//
//	// aliases
//	,	NRule = Static           // number of Tflag's allowed in rule definition
//	,	NDyn  = Phony            // number of Tflag's allowed inside flags
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	// not recorded in server book-keeping
//	enum class ExtraTflag : uint8_t { // flags for targets, not recorded in server book-keeping
//		Top
//	,	Ignore
//	,	Optional
//	,	SourceOk                      // ok to overwrite source files
//	,	Allow                         // writing to this target is allowed (for use in clmake.target and ltarget)
//	,	Late                          // target was written for real, not during washing
//	//
//	// aliases
//	,	NRule = Allow                 // number of Tflag's allowed in rule definition
//	} ;
//	// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO
//		Tflags      tflags       = {} ;                                   // if kind>=Target
//		Dflags      dflags       = {} ;                                   // if kind>=Dep
//		ExtraTflags extra_tflags = {} ;                                   // if kind>=Target
//		ExtraDflags extra_dflags = {} ;                                   // if kind>=Dep
//		// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class Comment : uint8_t {
//		None
//	// syscalls
//	,	access
//	,	canonicalize_file_name
//	,	chdir
//	,	chmod
//	,	chroot
//	,	creat                  , creat64
//	,	dlmopen
//	,	dlopen
//	,	execv                  , execvDep
//	,	execve                 , execveDep       , execveat          , execveatDep
//	,	execvp                 , execvpDep
//	,	execvpe                , execvpeDep
//	,	                                           faccessat         , faccessat2
//	,	fchdir
//	,	                                           fchmodat
//	,	fopen                  , fopen64
//	,	freopen                , freopen64
//	,	                                           fstatat           , fstatat64
//	,	                                           futimesat
//	,	getdents               , getdents64
//	,	getdirentries          , getdirentries64
//	,	glob                   , glob64
//	,	la_objopen
//	,	la_objsearch
//	,	link                                     , linkat
//	,	lstat                  , lstat64
//	,	lutimes
//	,	mkdir                                    , mkdirat
//	,	mkostemp               , mkostemp64
//	,	mkostemps              , mkostemps64
//	,	mkstemp                , mkstemp64
//	,	mkstemps               , mkstemps64
//	,	mount
//	,	                                           name_to_handle_at
//	,	                                           newfstatat
//	,	oldlstat
//	,	oldstat
//	,	open                   , open64          , openat            , openat64     , openat2
//	,	open_tree
//	,	opendir
//	,	readdir                , readdir64       , readdir_r         , readdir64_r
//	,	readlink                                 , readlinkat
//	,	realpath
//	,	rename                                   , renameat          , renameat2
//	,	rmdir
//	,	scandir                , scandir64       , scandirat         , scandirat64
//	,	stat                   , stat64
//	,	statx
//	,	symlink                                  , symlinkat
//	,	truncate               , truncate64
//	,	unlink                                   , unlinkat
//	,	utime
//	,	                                           utimensat
//	,	utimes
//	,	                                           __fxstatat        , __fxstatat64
//	,	                                           __lxstat          , __lxstat64
//	,	__open                 , __open64
//	,	__open_2               , __open64_2      , __openat_2        , __openat64_2
//	,	__open64_nocancel
//	,	__open_nocancel
//	,	__readlink__chk                          , __readlinkat_chk
//	,	__realpath_chk
//	,	__xstat                , __xstat64
//	// lmake functions
//	,	Analyzed
//	,	CheckDeps        , CheckTargets // not Chk... as name is seen by user
//	,	ComputedCrcs
//	,	CreateCodec                     // not Creat... as name is seen by user
//	,	Decode
//	,	DepAndTarget
//	,	Depend
//	,	Encode
//	,	EndJob           , EndOverhead
//	,	EnteredNamespace
//	,	Hot
//	,	Kill
//	,	List
//	,	LostServer
//	,	OsInfo
//	,	Panic
//	,	StartInfo
//	,	StartJob         , StartOverhead
//	,	StaticDep        , StaticDepAndTarget
//	,	StaticExec
//	,	StaticMatch
//	,	StaticTarget
//	,	Stderr           , Stdin              , Stdout
//	,	StillAlive
//	,	Timeout
//	,	Target
//	,	Tmp
//	,	Trace
//	,	UnexpectedTarget
//	,	Unstable
//	,	UploadedToCache
//	,	Wash             , Washed
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class CommentExt : uint8_t {
//		Bind
//	,	Dir
//	,	Direct
//	,	Err
//	,	File
//	,	Last
//	,	LdLibraryPath
//	,	Killed
//	,	Link     // not Lnk as name is seen by user
//	,	NoFollow
//	,	Orig
//	,	Overlay
//	,	Proc
//	,	RunPath
//	,	Read
//	,	Reply
//	,	Stat
//	,	Tmp
//	,	Unlink   // not Unlnk as name is seen by user
//	,	Verbose
//	,	Write
//	} ;
//	using CommentExts = BitMap<CommentExt> ;
//	// END_OF_VERSIONING
//		// START_OF_VERSIONING CACHE JOB REPO CODEC
//		using CodecCrc = Hash::Crc96 ;                                                                              // 64 bits is enough, but not easy to prove
//		static constexpr char CodecSep    = '*'       ; //!                                                    null
//		static constexpr char DecodeSfx[] = ".decode" ; static constexpr size_t DecodeSfxSz = sizeof(DecodeSfx)-1 ;
//		static constexpr char EncodeSfx[] = ".encode" ; static constexpr size_t EncodeSfxSz = sizeof(EncodeSfx)-1 ;
//		// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	enum class FileTag : uint8_t { // FileTag is defined here as it is used for Ddate and disk.hh includes this file anyway
//		None
//	,	Unknown
//	,	Dir
//	,	Lnk
//	,	Reg                        // >=Reg means file is a regular file
//	,	Empty                      // empty and not executable
//	,	Exe                        // a regular file with exec permission
//	//
//	// aliases
//	,	Target = Lnk               // >=Target means file can be generated as a target
//	} ;
//	// END_OF_VERSIONING
//	// START_OF_VERSIONING CACHE JOB REPO
//	// PER_FILE_SYNC : add entry here
//	enum class FileSync : uint8_t { // method used to ensure real close-to-open file synchronization (including file creation)
//		Auto
//	,	None
//	,	Dir                         // close file directory after write, open it before read (in practice, open/close upon both events)
//	,	Sync                        // sync file after write
//	} ;
//	// END_OF_VERSIONING
