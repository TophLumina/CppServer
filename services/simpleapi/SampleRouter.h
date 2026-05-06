#pragma once

#include <algorithm>
#include <charconv>
#include <optional>
#include <random>
#include <string>
#include <system_error>

#include "RouterModule.h"

namespace CppServer::Routers {
template <typename TContext>
class SampleRouter final : public CppServer::Routing::RouterModule<TContext> {
public:
  std::string RouterName() const override { return "SAMPLE"; }

  void Register(httplib::API::Router<TContext> &router) override {
    using Self = SampleRouter<TContext>;

    router.Get(
        "/sample/asciiart", "Sample ASCII Art",
        "Return ASCII text art for Markdown/Reddit/Manifold style rendering",
        "ASCII text", [](const httplib::Request &) { return asciiart; },
        httplib::API::RouteOptions{.content_type =
                                       "text/plain; charset=utf-8"});

    router.Get(
        "/sample/randomint", "Sample random integer",
        "Return a Mersenne Twister integer in the inclusive [min, max] "
        "interval. Query params: min, max.",
        "Random integer or null when query params are missing or invalid",
        [](const httplib::Request &req) -> std::optional<int> {
          const auto min_value = Self::ParseIntParam(req, "min");
          const auto max_value = Self::ParseIntParam(req, "max");
          if (!min_value.has_value() || !max_value.has_value()) {
            return std::nullopt;
          }

          const auto [lower_bound, upper_bound] =
              std::minmax(*min_value, *max_value);
          return Self::GenerateRandomInt(lower_bound, upper_bound);
        },
        httplib::API::RouteOptions{
            .parameters = {httplib::API::Parameter<int>(
                               httplib::API::ParameterLocation::Query, "min",
                               "Inclusive lower bound for the random integer.",
                               true, httplib::API::Json(3)),
                           httplib::API::Parameter<int>(
                               httplib::API::ParameterLocation::Query, "max",
                               "Inclusive upper bound for the random integer.",
                               true, httplib::API::Json(7))}});
  }

private:
  static std::optional<int> ParseIntParam(const httplib::Request &req,
                                          const char *name) {
    if (!req.has_param(name)) {
      return std::nullopt;
    }

    const std::string value = req.get_param_value(name);
    int parsed_value = 0;
    const auto [parse_end, parse_error] = std::from_chars(
        value.data(), value.data() + value.size(), parsed_value);
    if (parse_error != std::errc() ||
        parse_end != value.data() + value.size()) {
      return std::nullopt;
    }

    return parsed_value;
  }

  static int GenerateRandomInt(int min_value, int max_value) {
    thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<int> distribution(min_value, max_value);
    return distribution(generator);
  }

  inline static constexpr const char asciiart[] = R"ASCII(
    [WWWWWWWEEEEEEEWWEENNNMMMMMNNNMg_   NNEMMMMMMMMMMMMMNNNNNNNNNNNNEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEWNM[%[EWW[[[[[[N
    [[[[[[[[[WWWWWWWWWWEEEEEWEENNNMNNMNMNNMMMMMMMMMMMMMMMNNNNNNNNNNNNNNNNNNNNNNNNEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEN@EEEEEEEEENEg_$[NW[[[[[[[E,
    [[[[[[[[[[[[[[[[WWWWWWWEEEEEEEEWEENNMNMMMMMMMMMMMMMMMNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE, "WNEEEEENQ[[k%NW[[[[[[[EN
    [[[[[[[[[[[[[[[[[[[[[[WWWWWEEEEEEEEEEEWENMMMMMMMMMMMMMMMNNNNNNNNMMMMMMMNMNMNNNNNNNNNNNNNNNNNNNNNNNNEEEEEEEEEEEEEKYNEEEEEEEEEEEEEN,   EEEEEEENN[k@U#U#[[[[WN,
    [[[[[[[[[[[[[[[[[[[[[[[[[[[WWWWWEEEEEEEEEEEWENMMMMMMMMMMMMMNENEENNNNMMMMMNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNEEEEEENN ^%EEEEEEEEEEEENEkqqEEEEEEEEEEN[N##N[[[[[EK
    [[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[WWWWWEEEEEEEEEEWENMNMMMMMMMMMMNNNEEENNNNNNNNNNNNNNNNNNNNNNNNNNNNNJNNNNNNNNEEEEEN&  ~NEEEEEEEEEEENN[[$NEEEEEEEEEEEK##N[[[WWN
    [[[[[[[[[[[[[[[[[[[[[[[[[[[#####[[[[[[[WWWEEEEEEEEEEWWN@MMNMMMMMMMMMMMNEEEENNNNNNNNNNEEEEEEEEEEEE[k^NEEEEEEEEEEEE[k,  $NEEEEEEEEEEENN#EEEEEEEEEEEEN8##N[WWEEL
    [[[[[[[[[[[[[[[[############[[[[[[[[####[[[[WWEEEEEEEEEEWENMMMENMMMMMMMMMMNNNEEEEEEEEEEEEEEEEEEEN[[L %NEEEEEEEEEENN[EEENNNNNNNNNNNNNNNNNNNNNNNNEEEEN#[#NWEEEE
    [[[[[[[[[[[#############[[[[[[[[[[[[[[[[[###[[[[WWEEEEEEEEEEWENMMMMENNNNNMMMMMMMNNNEEEEEEEEEEEEEN[[[kjEEEEEEEEEEENENQ[ENNNNNNNNNNNNNNNNNNNNNNNNNNNNN###[EEEENL
    ##################[[[[[[[[[[[[[[[[[[[[[[[[####[[[[WWEEEEEEEEEEEEEN@MMMMENMNNNNNENNMNNNNNEEEEEEEEEN[[[QNEEEEEEEEEEEEEENEEEEENNNNNNNNNNNNNNNNNNNNNNNNN8####EEEEN
    [[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[#####[[[[WWEEEEEEEEEEEEEEEWENMMMMMENMNNNENNNNNENNNEEEEEEEN&[$NEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEENNNNNNNNNNU####WNNNNk
    [[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[####[[EEEEEEEENNNWWWWWWWWWW[WWWNENNNNNMMN@NMNEEEENNNNNNNNEEEENENEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE[[###[[[#####FNNNNg
    ENNNEEQQ[[[##[[[[[[[[[[[[[[[##[####[[[QQENNNNWWW#[[#[[%[[[[[[[[[[[[[[[###QWNNEENNNENNNMNEEEEEENNNEEENNEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEENNNNNNgggg[|#FWm__
    EEEEEEEEEEEEEEENNNNNNNNNNEEEEF~^^~~F~~~~"?F**WNE[[[[[[W[[[[[[[[[[[[[[[[[[E[[##WNEEEENNNNNNNNEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEENNNEEEEEEEEEEEEEEEENNMg[[[WWwq_
    EEEEEEEEEEEEEEEEEEEEEEEEEEEEW'    L          /      ~"N[[[[[[[[[[[[[[[[[[R[[[[[[#WNEEEEENNNNNNNEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEENNNNNNNNNEEENNNEEEEEEEEEEEEENNMg[#|[#EWwq__
    EEEEEEEEEEEEEEEEEEEEEEEEWWWW)    L          J         ~^"?W[[[[[[[[[[[[[[N[[[[[[[[[[WNEEEEEEENNEENNEEEEEEEEEENNNNEEEEEEEEEEEEEEEEEEEEEEEEEEEENNNNNNNNNNNNNNNNNNNNNNEEEEENNNgq####|#WWNmg__
    EEEEEEEEEEEEEEEEEEEEWWW[[[[F   _^          J          }      ^?E[[[[[$[[[R[[[[[[[[[[[[##NEEEEEEENNEEEEEEEEEEEEEEEENNNEENEEEEEEEEEEEEEEEEEEEEEEEEEENNNNNNNNNNNNNNNNNNNNNNNNNEEENNgg[######|[[WWWw
    EEEEEEEEEEEEEEEEEWWW[[[[[[L{   '          J          {{          ^~W[$[[[E[[[[[[[[[[[[[[[#RWNEEEEEENNNEEEEEEEEEEEEEEEEEEEEENNNEEEEEEEEEEEEEENNNEEEEEEENNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNMgj########
    EEEEEEEEEEEEEWWW[[[[[[##Q^j} _'          4~          |{              {"E[E[[[[[[[[[[[[[[[[R[##WNEEEEENNEEEEEEEEEEEEEEEEEEEEEEENEEEEEEEEEEEEEENEEEEEEEEEEENNNNNNNNNNNNNNNNNNNNNNNNNNNNNNENNNg[###
    EEEEEEEEEWWWW[[[[[[####['jE _~          4[           } ,                 {^"W[[[[[[[[[[[[#N[[[[#[WNEEEEENNNEEEEEEEEEEEEEEEEEEEEEEENEENNNNNNEEENEEEEEEEEEEEENNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNENNN
    EEEEEWWWW[[[[[[######[[L E}_'         _NF            ' }              L  {    ~"W[[[[[[[#EN[[[[[[[[[WNWNEEEENEEEEEEEEEEEEEEEEEEEEEEEEEENENNNNEEENENEEEEEEEEEENNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN
    WWWWW[[[[[[#####[####[F NE_^         jN['              {              |   ,       ^*W[[#EEN[[[[[[[[[QNNE%[WNEENEEEEEEEEEEEEEEEEEEEEEEEEEEEEENNNNNNENNNEEEEEEENNNNNNNNNNNNMMMMNNNNNNNNNNNNNNNNNNN
    [[[[[[[[#####[[[[[[##F NQNL         4[#'            {                 ^   ,          ^~EN~E[[[[[[[[[QL ^M##NQWNEENENEEEEEEEEEEEEEEEEEEEEEEEEENNNNNNNNNNNNEEEEEENNNNNNNNNNMMMMMMMMMNNNNNNNNNNNNNN
    [[[[[#####[[[[[[[[[[W A#QL         N#R`\            [  {,              ,  }           4F  W[[[[[[[[#F   ~N[[#[WQWNEEEENNNENNEEEEEEEEEEEEEEEEEENNNNNNNNNNNNEEEEEENNNNNNNNNMMMMMMMMMMMMMMNNNNNNNNN
    #########[[[[[[[[[[Q'{##~        jW##%  ,           L  {}              \  |          @^   } ^*[[[[[Q'    ^W[[[[##WQFNEEENNNNNNNNNNEEEEEEEEEEEEENNNNNNNNNNNNNEEEEENNNNNNNNNMMMMMMMMMMMMMMMMMMMNNN
    #######[[[[[[[[[[[#F R[F        4[##~ L \              {{                 {         F     L    ^?EQ~       U[[[[W[[[W%FNEEEEENNNNNNNNNNEEEEEEEEEENNNNNNNNNNNNNEEEEEENNNNNNNNMMMMMMMMMMMMMMMMMMMM
    #####[[[[[[[[[[[[[E $[F        N###"  {  .         {   { ,              \  ,       F      _,qgggggK,       {#[[[#U[[[[RW%8WNEEEENNNNNNNNNNEEEEEEEEENNNNNNNMNNNNNEEEENNNNNNNNNMMMMMMMMMMMMMMMMMMM
    ###[[[[[[[[[[[[[[#~ UF       jN###F    k  ,        [   { }                 L      ggmNF"~{`      F          W[[[[%[[[Q#[[#WNg[WNEEEENMNNNNNNNNNNEEEEENNNNNNNMMMNNNNENENNNNNNNNNMMMMMMMMMMMMMMMMM
    [[[[[[[[[[[[[[[[[F JW       J[###F     {, `        }   J                 \ $ _a*FF`      /      /           ~[[[[#[[QU[[[[QW#E*@[[WNEENENMMMMNNNNNEENENNNNNNNNMMNNNNNENNNNNNNNNNMMMMMMMMMMMMMMMM
    [[[[[[[[[[[[[[[[Q' N~      @####F       %  `,      L   $  L         L     ;{^   F        '     F             $#[[###N[[[#F#[[E %MNNg[#WEEENMNNMMMMNNNNENNNNNNNMMMNNNNNNNNNNNNNNNNMMMMMMMMMMMMMMM
    [[[[[[[[[[[[[[[[E {}     _NR###W         L  ~      '   W  {,         ,      ,  /        {     F_.= _         ^,?[#U&#[[Q[[[[#E$NNEENNFWg#FWEENNMENNMMMMMMNNNNNMMMMNNNENNNNNNNNNNNNMMMMMMMMMMMMMM
    [[[[[[[[[[[[[[[#L 4     JE#U###`         {,       {    E   &         {      k J         L   j~_ggF^   __-     [  ^W#[[F#[[#[#K$NNMK~}[``~"Wg#FWENNNNMNNNMMMMMMMMMMMEEEEENNNNNNNNNNNMMMMMMMMMMMMM
    ~?N#[[[[[[[[[[[Q'j'    4W[#N##)           %     . {    F   ^,         l     {j'        {  _4EEEEEEENEEEgm..   { _'  FW[[[[W[#L{|[Z|[_L      NWWm[FWNENNNMMMENNMMMMMMEEEEEMEENNNNNNNNMMMMMMMMMMMM
       ~*N[[[[[[[[[# F    4#E##E#E             k      [    }    $          t     K       _qNNNNEENFF"]~`           k  /`   "EL~W#' 'O||_Eg,    4####[[WNMQFNENNMMMMMMMEEEEEEENMEENNNNNNN@MMMMMMMMMMM
          ~?%Q[[[[[#g    @##R##$#}             Jk     }    L     ,          k    { =mg@NNEEENNNNNNk   `          _F_=`L     /  \      `~~      N##########[#WFNNNMMMMMMDEEEEEENMNMMMMNMMMENNNNNNMMNN
              ^?W[##L   E#[#N##$#L              %k    }          ^           k    LgNEENNNNMNF[[~||L            4=^ L '    /    V_             U######R######N WNNNEENNNEEEEEEEENMNEEEENMMMMENMMMNNN
                  ~$   E#[[#N##QU}              {[%   L   {,      (         ._LggNENNNNNNNMMM[|||"`|                |     <      "             U#####[#######K  UNEEEENEEEEEEEEEEEEEEEEEEENMMMM}^~
                   L  F^?WNND##FN[   ;           R#N,j    {\       L         ` k  ?@NEN[||ZWK||||qqL                {{  j'                    {N############Q}  $#NEEEMNNEEEEEEEEEEEEEEEEEENEENM
                  {  4      W##'EE               {##[N    A ,      ^,         . \   L ^*#qjggg_qgF^                  L /           !,         {N####&#######E   {[[NENMNNEEEEEEEEEEEEEEEEEEEEEEMk
                  K {'      {#W {[,               R##$    } {       \         ^  ", ^,       ~"   ~                 /y'             ^*        {EU##Q#######UL    U#ENEENNEEEEEEEEEEEEEEEEEEEEENMMk,
                  } L       ^UL  NL               $##E    L  }       '         ;  ^; ^,                            gF                         $QR##U######FF     $##EEEEEEEEEEEEEEEEEEEEEEEEENMMMMK
                  }@         E   %#               {##K    '  {        L             h ^L                          F`                         {UQU#W######E{L     {#[EEEEEEEEEEEEEEEEEEEEEEEENMMMMMK
                 -bK         {    E,               U#K   jg   }        ,        .   {^_ %                                                    N#{R#R######'N      {[[NNEEEEEEEEEEEEEEEEEEEEEEMMMMMM}
                 .N}          L { ^K    '          $#}   {[#Ngj,       ^,            K \,^q                                                 {[#{EW######^}N      {R#E^NEEEEEEEEEEEEEEEEEEEEEMMMMMM}
                  K[          $    ^L              {#L   {###[[K         ,       ,   $,  *,",                                              {W##$ ?#####^4 E      {U#W ~NEEEEEEEEEEEEEEEEEEENMMMMMML
                  }|          ^,    ~L              UL   {#####W         ^,      \   {k    ~w]q                                           gU###E;  {##`J' E      {[#F   ?NEEEEEEEEEEEEEEEENMMMMMMM-
                  L{           {     ~g   ;         N'   {######k          v      ,  {W       `~h,                           ,mF"       _N[####E     `j'  $      {[Q~     Y%EEEENEEEEEEEENMMMMMMMM,
                  L L           L,    ~k            %,   {######E           \     \   $            '                                  _4W######K      L   {      $[F       ~@MMMMENENEEENMMMMMMMMMF
                  } ~,          $|     ^%           {L   {#####Q[L           \,    ,  E,                                            _4F[#######L     F    {      NF         4NMMMMMNEEEEEMMMMMMMMM'
                  |  ^q         ^M       Y, ;       {}   {R####$#$            `,   {  {}                                          _@N[########[L    4,    {     jF          ^NEENEEEEEEEEENMMMMMMD
                   k   *_        W        ~L         }    U####&##L             t   V  N     "Wgg,                               4N[####[[####Q    {[}    {     "             ENEENEEEEEEE$MMMMMMML
                    \    ?q      {L        ^k`       }    N####U##E ;            \   , N        ~"?Ngg,                        gN[#######[####$   jW[E     L                  EEEEEEENMMMMMMMMMMMMM
                     ^_     '-,  {&         {N_      $    W###QR###L ,            `, ~ {,             ~"?*mg,__              _NW[#############E   L U#     }                  $MMMEEEEEEENMMMMMMMMML
                       \,      'wj{       ;  %Wk     {    {###{####N {,            ^  \{}                  4[[#WENNNNMmmmgggNN##[[#########[#[}  4  {[L    W                  $MMMMEEEEEEEEMMNMMMMMM
                         >,      {{           %[%,   {,    U##E####[K N,            j  VK                 N####R########%###################[QL jK   %E    {                  NMMMMDNEEEEEEENMMMEMMM
                           "q    { ,       }  ^U#Wk   L    {##U######k^Ug            \  W               _N#####R##################%##########W  @N,   %L   {                  $MMMMMEEEEEEEEENMMMMMM
                             "g  ){}       { , ~U#[N, {     U#[######W,{Ek            L {               N[#####[########[[[[Q[#####W#########K {UEE    W    L                 ENNNMMMEEEEEEEENMMMMMM
                               \J J}         {  {[##Wg k    ~NU#######E,%NM,          ^ {NMMNMggggg___,$U######[[EN*"?"~~~~~~^"WNQ##[########} $#$#E    L   k                 EEEENNMNEEEEEEENNMMMMM
                               _' WK        , L  W###[K{,    {N#######[N,WNMk          } ENMMNENNNNNNNKE[[QgEMMNMMk,             ^WNN#######Q  U#W[#k    ,  {                {EEEEEENNEEEEEEENNMMMMM
                               ' E#K        , {  {[###[N$     ^U######E#N %NNM,         ,{NNNNNNNNNNNNNNNNNNNNNNNNMEN              ~%N######E {R#PU##L   ^,  L               {NEEEEEEENEEEEEEENNMMMN
                              L {[#K        L  L  %#####[N      %[###EO#NN,~NNMk,       \ ENNNNNNMNNL ENNNNNNNNMNMMNE                {N#####} {##L$###,   ^, $               {NNEEEEEEEEEEENNNWWEEEE
                             ^  R##K        ~  K  {N#####[N,     ~WNNNNF??""""WNMM,      \YE@MNNNMg$NMENNMMNNNNNNNMME                 ~U###Q' $##L{R###L   {,{,              {Q[[[#WWWWW#[[[####EEEE
                           _^  A###L           ~,  E########g      ~*q,  _gg@NNNNENMg,    tNUEMMNNNNK/NNMMMNNNNNNNNMN,                 {U##E  $##L %####k   {,k              {NN################[EEE
                          j'  j[###}       {  4 }  {U#######[Nw,      {NF~`        ~~WN,   kYk{ENMNNNNNMMMMNNNNNMMN@NL                  N##}  $##}  U####k   \j              {NEN[###############WEE
    
---
asciiart.club
    )ASCII";
};
} // namespace CppServer::Routers