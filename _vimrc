set nocompatible
"source $VIMRUNTIME/vimrc_example.vim
"source $VIMRUNTIME/mswin.vim
"behave mswin
 
"colorscheme vibrantink
"colorscheme vividchalk
"colorscheme jellybeans
let g:load_doxygen_syntax=1
set ff=unix
colorscheme candy
set matchpairs+={:},(:),[:],<:>
set guioptions=gR
set showtabline=2
set ignorecase
set smartcase
set incsearch
set hlsearch
filetype plugin indent on
set autowrite
syntax on
set cindent
"set cinoptions=4,g0,t0,c0,(s,(0,w1,(0,W4,(s,m1,:0,b1,l1
"set cinoptions=(0,l1,g0
set cinoptions=(s,(0,W4,l1,g0
"set cino=>2,:0,=0,l1,g0,t0,c0,(0,w1,(s,m1,)100,*100
"set autoindent
set autoread
set cmdheight=2
set textwidth=80
set columns=80
set cursorline
set errorbells
set fo=tcrn
set foldcolumn=2
set foldmethod=indent
set foldenable
set foldminlines=50
set foldopen=all
if has("win32")
  set guifont=Consolas:h11
endif
if has("unix")
  set guifont=Liberation\ Mono\ 12
endif
set history=100
set ruler
set scrolloff=2
set shiftwidth=2
set showbreak="+++"
set showmode
set showcmd
set showfulltag
set showmatch
"set smartindent
set smarttab
set expandtab
set splitright
set statusline=%<%f%=\ [%1*%M%*%n%R%H]\ %-19(%3l,%02c%03V%)%O'%02b'
set number
set numberwidth=4
set tabstop=2
set visualbell
set wrap
set wildmenu
set showfulltag
set display+=lastline
set printoptions=syntax:y,wrap:y
highlight OverLength ctermbg=red ctermfg=white guibg=#592929
match OverLength /\%81v.*/
" configure tags - add additional tags here or comment out not-used ones
set tags+=C:/opt/utils/tags/msvcrt_tags
set tags+=C:/opt/utils/tags/stl_tags
set tags+=C:/opt/utils/tags/winsdk_tags

"switch tabs
nnoremap <silent> <C-n> :tabnext <CR>
nnoremap <silent> <C-p> :tabprevious <CR>
 
let OmniCpp_NamespaceSearch = 1
let OmniCpp_GlobalScopeSearch = 1
let OmniCpp_ShowAccess = 1
let OmniCpp_MayCompleteDot = 1
let OmniCpp_MayCompleteArrow = 1
let OmniCpp_MayCompleteScope = 1
let OmniCpp_DefaultNamespaces = ["std", "_GLIBCXX_STD"]
"automatically open and close the popup menu / preview window
au CursorMovedI,InsertLeave * if pumvisible() == 0|silent! pclose|endif
set completeopt=menuone,menu,longest,preview
 
" build tags of your own project with CTRL+F12
map <C-F12> :silent !ctags -R --c++-kinds=+p --fields=+iaS --extra=+q .<CR>

" switch between header/*.c/*.cpp
map <C-O><C-O> :A <CR>
map <C-O><C-H> :AS <CR>
map <C-O><C-V> :AV <CR>
map <C-O><C-T> :AT <CR>
 
" OmniCppComplete
let OmniCpp_NamespaceSearch = 1
let OmniCpp_GlobalScopeSearch = 1
let OmniCpp_ShowAccess = 1
let OmniCpp_MayCompleteDot = 1
let OmniCpp_MayCompleteArrow = 1
let OmniCpp_MayCompleteScope = 1
let OmniCpp_DefaultNamespaces = ["std", "_GLIBCXX_STD"]
" automatically open and close the popup menu / preview window
au CursorMovedI,InsertLeave * if pumvisible() == 0|silent! pclose|endif
set completeopt=menuone,menu,longest,preview
 
let g:EchoFuncLangsUsed = ["java","cpp","c"]
let g:EchoFuncKeyNext = "Alt++"
let g:EchoFuncKeyPrev	= "Alt+="

