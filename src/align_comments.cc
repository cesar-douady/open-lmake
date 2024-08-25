// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "lib.hh"

using namespace Disk ;

ENUM(LineKind
,	Blank
,	Comment
,	Plain
)

struct Line {                                // by default, construct a blank line
	size_t   lvl         = 0               ;
	LineKind kind        = LineKind::Blank ;
	::string pfx         ;
	::string code        ;
	size_t   code_len    = 0               ; // with tab expansion
	::string comment     ;
	size_t   comment_pos = 0/*garbage*/    ; // filled in by optimize()
} ;

size_t   g_tab_width    = 0/*garbage*/ ;
size_t   g_max_line_sz  = 0/*garbage*/ ;
::string g_comment_sign ;

::vector<Line> get_lines(const char* file) {
	::vector<Line> res   ;
	::vector_s     lines ;
	//
	if (file) lines = read_lines(file) ;
	else      for( ::string l ; ::getline(::cin,l) ;) lines.push_back(l) ;
	//
	for( ::string const& l : lines ) {
		size_t   lvl   = 0               ;
		size_t   cnt   = 0               ;
		size_t   start = 0               ;
		LineKind kind  = LineKind::Blank ; for( char c : l ) if (!::isspace(c)) { kind = LineKind::Plain ; break ; }
		for( size_t i : iota(l.size()) ) {
			if (l[i]=='\t') {
				lvl++ ;
				cnt   = 0   ;
				start = i+1 ;
			} else {
				cnt++ ;
				if (cnt==g_tab_width) break ;
			}
		}
		//
		if (kind==LineKind::Blank) {
			res.emplace_back() ;
		} else {
			if (l.substr(start).starts_with(g_comment_sign)) kind = LineKind::Comment ;
			size_t code_len    = 0/*garbage*/                     ;
			size_t comment_pos = l.find(' '+g_comment_sign,start) ;
			if (comment_pos==Npos) {
				comment_pos = l.size() ;
			} else {
				comment_pos++ ;
				if (l[comment_pos+g_comment_sign.size()]=='!') comment_pos = l.size() ;
			}
			for ( code_len=comment_pos ; code_len&&l[code_len-1]==' ' ; code_len-- ) ;
			res.push_back(Line{ lvl , kind , l.substr(0,start) , l.substr(start,code_len-start) , g_tab_width*lvl+code_len-start , l.substr(comment_pos) }) ;
		}
	}
	return res ;
}

struct Info {
	// cxtors & casts
	Info( bool ko_ , size_t n_lvls ) : ko{ko_} , breaks(n_lvls) {}
	// services
	bool operator<(Info const& other) const {
		/**/                                      if (ko         !=other.ko         ) return ko         <other.ko          ;
		/**/                                      if (n_closes   !=other.n_closes   ) return n_closes   <other.n_closes    ;
		for( size_t i=breaks.size() ; i>0 ; i-- ) if (breaks[i-1]!=other.breaks[i-1]) return breaks[i-1]<other.breaks[i-1] ;
		/**/                                                                          return glb_pos    <other.glb_pos     ;
	}
	// data
	bool             ko       = true         ;
	size_t           n_closes = 0            ;
	::vector<size_t> breaks   = {}           ;
	size_t           glb_pos  = 0            ;
	size_t           prev_x   = 0/*garbage*/ ;
} ;

struct Tab {
	// cxtors & casts
	Tab( size_t h_ , size_t w_ , size_t nl ) : h{h_} , w{w_} , tab{h*w,Info(true/*ko*/,nl)} {}
	// accesses
	::vector_view<Info> operator[](size_t l) { return { &tab[l*w] , w } ; }
	// data
	size_t         h   ;
	size_t         w   ;
	::vector<Info> tab ;
} ;

// use dynamic programming to find best comment alignment
void optimize( ::vector<Line>& lines) {
	size_t         n_lvls = 0                                         ; for( Line const& l : lines ) n_lvls = ::max(n_lvls,l.lvl+1) ;
	Tab            tab    { lines.size() , g_max_line_sz , n_lvls+1 } ;
	Line           l0     ;
	::vector<Info> t0     { tab.w , {false/*ko*/,n_lvls+1}          } ;
	//
	size_t py         = Npos ;
	size_t break_lvl1 = 0    ;                                                                      // minimum indentation level of line separating comments, 0 is reserved to mean blank line
	for( size_t y : iota(tab.h) ) {
		Line const&         l  =            lines[y ]                           ;
		::vector_view<Info> t  =            tab  [y ]                           ;
		::vector_view<Info> pt = py!=Npos ? tab  [py] : ::vector_view<Info>(t0) ;
		if (!l .comment) {
			if (l.kind==LineKind::Blank) break_lvl1 = 0                         ;
			else                         break_lvl1 = ::min(break_lvl1,l.lvl+1) ;
			continue ;
		}
		py = y ;
		size_t px = 0     ;
		Info   pi = pt[0] ;
		for( size_t x : iota(1,pt.size()) )
			if (pt[x]<pi) { pi = pt[x] ; px = x ; }
		if (break_lvl1) pi.breaks[break_lvl1-1]++ ;
		break_lvl1 = n_lvls+1 ;
		size_t code_len_above = 0 ;                                                                 // ensure comments are not too close to code
		size_t code_len_below = 0 ;                                                                 // ensure comments are not too close to code
		if ( y>0       && lines[y-1].kind==LineKind::Plain ) code_len_above = lines[y-1].code_len ;
		if ( y<tab.h-1 && lines[y+1].kind==LineKind::Plain ) code_len_below = lines[y+1].code_len ;
		for( size_t x=l.code_len+1 ; x+l.comment.size()<=tab.w ; x++ ) {
			if (pt[x]<pi) { t[x] = pt[x] ; t[x].prev_x = x  ; }
			else          { t[x] = pi    ; t[x].prev_x = px ; }
			if (x<code_len_above+1) t[x].n_closes++ ;
			if (x<code_len_below+1) t[x].n_closes++ ;
			/**/                    t[x].glb_pos += x ;
		}
	}
	if (py==Npos) return ;                                                                          // nothing to optimize
	size_t              min_x  = 0       ;
	::vector_view<Info> last_t = tab[py] ;
	for( size_t x : iota(1,last_t.size()) )
		if (last_t[x]<last_t[min_x]) min_x = x ;
	SWEAR(!last_t[min_x].ko) ;
	for( size_t y1=tab.h ; y1>0 ; y1-- ) {
		Line& l = lines[y1-1] ;
		if (!l.comment) continue ;
		l.comment_pos = min_x ;
		min_x = tab[y1-1][min_x].prev_x ;
	}
}

int main( int argc , char* argv[] ) {
	#if PROFILING
		::string gmon_dir_s ; try { gmon_dir_s = search_root_dir_s().first+GMON_DIR_S ; } catch(...) {}
		set_env( "GMON_OUT_PREFIX" , dir_guard(gmon_dir_s+"align_comment") ) ;                          // in case profiling is used, ensure unique gmon.out
	#endif
	if ( argc<4 || argc>5 ) exit(Rc::Usage,"usage : ",argv[0]," tab_width max_line_size comment_sign [file]") ;
	g_tab_width    = from_string<size_t>(argv[1]) ;
	g_max_line_sz  = from_string<size_t>(argv[2]) ;
	g_comment_sign =                     argv[3]  ;
	//
	::vector<Line> lines = get_lines(argc==5?argv[4]:nullptr) ;
	if (!lines) return 0 ;
	//
	optimize(lines) ;
	//
	::cout << ::left ;
	for( Line const& l : lines ) {
		if (+l.comment) ::cout << l.pfx << ::setw(l.comment_pos-g_tab_width*l.lvl) << l.code << l.comment <<'\n' ;
		else            ::cout << l.pfx <<                                            l.code <<             '\n' ;
	}
	return 0 ;
}
