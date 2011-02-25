/* 

                          Firewall Builder

                 Copyright (C) 2011 NetCitadel, LLC

  Author:  Vadim Kurland     vadim@fwbuilder.org

  This program is free software which we release under the GNU General Public
  License. You may redistribute and/or modify this program under the terms
  of that license as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
 
  To get a copy of the GNU General Public License, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

header "pre_include_hpp"
{
    // gets inserted before antlr generated includes in the header
    // file
}
header "post_include_hpp"
{
    // gets inserted after antlr generated includes in the header file
    // outside any generated namespace specifications

#include <sstream>

class PIXImporter;
}

header "pre_include_cpp"
{
    // gets inserted before the antlr generated includes in the cpp
    // file
}

header "post_include_cpp"
{
    // gets inserted after the antlr generated includes in the cpp
    // file
#include <antlr/Token.hpp>
#include <antlr/TokenBuffer.hpp>

#include "../libgui/PIXImporter.h"
}

header
{
    // gets inserted after generated namespace specifications in the
    // header file. But outside the generated class.
}

options
{
	language="Cpp";
}


class PIXCfgParser extends Parser;
options
{
    k = 2;
//    defaultErrorHandler=false;
}
{
// additional methods and members

    public:
    
    std::ostream *dbg;
    PIXImporter *importer;
}

cfgfile :
        (
            comment
        |
            version
        |
            hostname
        |
            ip_commands
        |
            intrface
        |
            controller
        |
            access_list_commands
        |
            exit
        |
            description
        |
            shutdown
        | 
            certificate
        |
            quit
        |
            unknown_command
        |
            NEWLINE
        )+
    ;

//****************************************************************

ip_commands : IP ( ip_access_list_ext | interface_known_commands | community_list_command | unknown_command )
    ;

//****************************************************************
quit : QUIT
        {
            consumeUntil(NEWLINE);
        }
    ;

//****************************************************************
community_list_command : COMMUNITY_LIST 
        {
            consumeUntil(NEWLINE);
        }
    ;

//****************************************************************
unknown_command : WORD 
        {
            consumeUntil(NEWLINE);
        }
    ;

//****************************************************************
certificate : CERTIFICATE WORD 
        {
            consumeUntil(NEWLINE);
            consumeUntil(QUIT);
        }
    ;

//****************************************************************
version : (PIX_WORD | ASA_WORD) VERSION_WORD NUMBER
        {
            importer->setDiscoveredVersion(LT(0)->getText());
            *dbg << "VERSION " << LT(0)->getText() << std::endl;
            consumeUntil(NEWLINE);
        }
    ;

//****************************************************************
hostname : HOSTNAME ( STRING | WORD )
        {
            importer->setHostName( LT(0)->getText() );
            *dbg << "HOSTNAME "
                << "LT0=" << LT(0)->getText()
                << std::endl;
        }
    ;

//****************************************************************
// note that permit_ext and deny_ext eat NEWLINE. This is necessary
// because the same parser rules are used for ip access-list commands,
// where they should work the same way as LINE_COMMENT which eats
// NEWLINE
//
access_list_commands : ACCESS_LIST acl_num:INT_CONST
        {
            importer->newUnidirRuleSet( std::string("acl_") + acl_num->getText() );
            *dbg << acl_num->getLine() << ":"
                << " ACL #" << acl_num->getText() << " ";
        }
        (
            permit_ext
        | 
            deny_ext
        |
            remark
        )
    ;

//****************************************************************

ip_access_list_ext : ACCESS_LIST name:WORD
        {
            importer->newUnidirRuleSet( name->getText() );
            *dbg << name->getLine() << ":"
                << " ACL ext " << name->getText() << std::endl;
        }
        (
            permit_ext
        |
            deny_ext
        |
            comment
        |
            remark
        |
            NEWLINE
        )
        {
            *dbg << LT(0)->getLine() << ":"
                << " ACL line end" << std::endl << std::endl;
        }
    ;

//****************************************************************
permit_ext: PERMIT 
        {
            importer->newPolicyRule();
            importer->action = "permit";
            *dbg << LT(1)->getLine() << ":" << " permit ";
        }
        rule_ext NEWLINE
        {
            importer->pushRule();
        }
    ;

deny_ext: DENY
        {
            importer->newPolicyRule();
            importer->action = "deny";
            *dbg << LT(1)->getLine() << ":" << " deny   ";
        }
        rule_ext NEWLINE
        {
            importer->pushRule();
        }
    ;

//****************************************************************
// the difference between standard and extended acls should be in these rules
rule_ext : 
        (
            ip_protocols
            hostaddr_ext { importer->SaveTmpAddrToSrc(); *dbg << "(src) "; }
            hostaddr_ext { importer->SaveTmpAddrToDst(); *dbg << "(dst) "; }
            (time_range)?
            (fragments)?
            (log)?
        |
            ICMP
            {
                importer->protocol = LT(0)->getText();
                *dbg << "protocol " << LT(0)->getText() << " ";
            }
            hostaddr_ext { importer->SaveTmpAddrToSrc(); *dbg << "(src) "; }
            hostaddr_ext { importer->SaveTmpAddrToDst(); *dbg << "(dst) "; }
            (icmp_spec)?
            (time_range)?
            (fragments)?
            (log)?
        |
            (TCP|UDP)
            {
                importer->protocol = LT(0)->getText();
                *dbg << "protocol " << LT(0)->getText() << " ";
            }
            hostaddr_ext { importer->SaveTmpAddrToSrc(); *dbg << "(src) "; }
            (xoperator { importer->SaveTmpPortToSrc();  } )?
            hostaddr_ext { importer->SaveTmpAddrToDst(); *dbg << "(dst) "; }
            (xoperator { importer->SaveTmpPortToDst();  } )?
            (established)? 
            (time_range)?
            (fragments)?
            (log)?
        )
        {
            *dbg << std::endl;
        }
    ;

//****************************************************************
// ip_protocols : (IP | AHP | EIGRP | ESP | GRE | IGRP | IPINIP | NOS | OSPF | PCP | PIM )
ip_protocols : (IP | WORD )
        {
            importer->protocol = LT(0)->getText();
            *dbg << "protocol " << LT(0)->getText() << " ";
        };

icmp_spec :
        (
            (INT_CONST) => (icmp_type:INT_CONST icmp_code:INT_CONST)
            {
                importer->icmp_type = icmp_type->getText();
                importer->icmp_code = icmp_code->getText();
                importer->icmp_spec = "";
                *dbg << icmp_type->getText() << " "
                    << icmp_code->getText() << " ";
            }
        |
            icmp_word:WORD
            {
                importer->icmp_spec = icmp_word->getText();
                *dbg << icmp_word->getText() << " ";
            }
        )
    ;


xoperator : single_port_op | port_range  ;

single_port_op : (P_EQ | P_GT | P_LT | P_NEQ )
        {
            importer->tmp_port_op = LT(0)->getText();
            *dbg << LT(0)->getText() << " ";
        }
        port_spec
    ;

port_range : P_RANGE
        {
            importer->tmp_port_op = LT(0)->getText();
            *dbg << LT(0)->getText() << " ";
        }
         port_spec port_spec
    ;

port_spec : (WORD|INT_CONST)
        {
            importer->tmp_port_spec += (std::string(" ") + LT(0)->getText());
            *dbg << LT(0)->getText() << " ";
        }
    ;

hostaddr_ext :
        (HOST h:IPV4)
        {
            importer->tmp_a = h->getText();
            importer->tmp_nm = "0.0.0.0";
            *dbg << h->getText() << "/0.0.0.0";
        }
    | 
        (a:IPV4 m:IPV4)
        {
            importer->tmp_a = a->getText();
            importer->tmp_nm = m->getText();
            *dbg << a->getText() << "/" << m->getText();
        }
    | 
        ANY
        {
            importer->tmp_a = "0.0.0.0";
            importer->tmp_nm = "0.0.0.0";
            *dbg << "0.0.0.0/0.0.0.0";
        }
        ;

hostaddr_std :
        (h:IPV4)
        {
            importer->tmp_a = h->getText();
            importer->tmp_nm = "0.0.0.0";
            *dbg << h->getText() << "/0.0.0.0";
        }
    | 
        (a:IPV4 m:IPV4)
        {
            importer->tmp_a = a->getText();
            importer->tmp_nm = m->getText();
            *dbg << a->getText() << "/" << m->getText();
        }
    | 
        ANY
        {
            importer->tmp_a = "0.0.0.0";
            importer->tmp_nm = "0.0.0.0";
            *dbg << "0.0.0.0/0.0.0.0";
        }
        ;

log : (LOG | LOG_INPUT)
        {
            importer->logging = true;
            *dbg << "logging ";
        }
    ;

established : ESTABLISHED
        {
            importer->established = true;
            *dbg << "established ";
        }
    ;

fragments : FRAGMENTS
        {
            importer->fragments = true;
            *dbg << "fragments ";
        }
    ;

time_range : TIME_RANGE tr_name:WORD
        {
            importer->time_range_name = tr_name->getText();
            *dbg << "time_range " << tr_name->getText() << " ";
        }
    ;


//****************************************************************

controller : CONTROLLER
        {
            importer->clearCurrentInterface();
            consumeUntil(NEWLINE);
        }
    ;

//****************************************************************

intrface  : INTRFACE in:WORD
        {
            importer->newInterface( in->getText() );
            *dbg << in->getLine() << ":"
                << " INTRFACE: " << in->getText() << std::endl;
        }
        NEWLINE
    ;

nameif  : NAMEIF in:WORD
        {
            importer->addInterfaceLabel( in->getText() );
            *dbg << in->getLine() << ":"
                << " INTRFACE LABEL: " << in->getText() << std::endl;
        }
        NEWLINE
    ;

// interface description
// Use it for comment
description : DESCRIPTION
        {
            *dbg << LT(1)->getLine() << ":";
            std::string descr;
            while (LA(1) != ANTLR_USE_NAMESPACE(antlr)Token::EOF_TYPE && LA(1) != NEWLINE)
            {
                descr += LT(1)->getText() + " ";
                consume();
            }
            importer->addInterfaceComment( descr );
            *dbg << " DESCRIPTION " << descr << std::endl;
            //consumeUntil(NEWLINE);
        }
    ;

//****************************************************************

// remark. According to the Cisco docs, can only be used
// within access list
// Use it for the current rule comment
remark : REMARK
        {
            *dbg << LT(1)->getLine() << ":";
            std::string rem;
            while (LA(1) != ANTLR_USE_NAMESPACE(antlr)Token::EOF_TYPE && LA(1) != NEWLINE)
            {
                rem += LT(1)->getText() + " ";
                consume();
            }
            importer->addRuleComment( rem );
            *dbg << " REMARK " << rem << std::endl;
            //consumeUntil(NEWLINE);
        }
    ;

shutdown : SHUTDOWN
        {
            *dbg<< LT(1)->getLine() << ":"
                << " INTERFACE SHUTDOWN " << std::endl;
        }
    ;

interface_known_commands :
        (
            intf_address
        |
            switchport
        |
            shutdown
        ) NEWLINE ;


// need this because "ospf", "bgp" and others are a known tokens
// (needed for protocol and ports in access lists) and "ip ospf" and
// similar are legit interface commands

// ignore_interface_commands : (BGP | OSPF | DHCP)
//         {
//             consumeUntil(NEWLINE);
//         }
//     ;

intf_address : ADDRESS a:IPV4 m:IPV4 (s:SECONDARY)? 
        {
            importer->addInterfaceAddress(a->getText(), m->getText());
            *dbg << LT(1)->getLine() << ":"
                << " INTRFACE ADDRESS: " << a->getText()
                << "/" << m->getText() << " ";
            if (s)
            {
                *dbg << s->getText();
            }
            *dbg <<  std::endl;
        }
    ;

switchport : SWITCHPORT ACCESS VLAN vlan_num:WORD
        {
        }
    ;

//****************************************************************

access_group_by_name : ACCESS_GROUP acln:WORD dir:WORD INTRFACE intf_label:WORD
        {
            importer->setInterfaceAndDirectionForRuleSet(
                acln->getText(),
                intf_label->getText(),
                dir->getText() );
            *dbg << LT(1)->getLine() << ":"
                << " INTRFACE: ACL '" << acln->getText() << "'"
                << " " << intf_label->getText()
                << " " << dir->getText() << std::endl;
        }
    ;

//****************************************************************

exit: EXIT
    ;

comment : (LINE_COMMENT | COLON_COMMENT) ;

// comment: COMMENT_START
//         {
//             *dbg << LT(1)->getLine() << ":";
//             std::string comm;
//             while (LA(1) != ANTLR_USE_NAMESPACE(antlr)Token::EOF_TYPE && LA(1) != NEWLINE)
//             {
//                 comm += LT(1)->getText() + " ";
//                 consume();
//             }
//             importer->addInterfaceComment( comm );
//             *dbg << " COMMENT " << comm << std::endl;
//         }
//     ;


//****************************************************************

class PIXCfgLexer extends Lexer;
options {
    k = 10;
    // ASCII only
    charVocabulary = '\3'..'\377';
}


tokens
{
    EXIT = "exit";
    QUIT = "quit";

    HOSTNAME = "hostname";
    CERTIFICATE = "certificate";
    
    INTRFACE = "interface";
    CONTROLLER = "controller";
    DESCRIPTION = "description";
    REMARK = "remark";
    SHUTDOWN = "shutdown";

    VLAN = "vlan";
    SWITCHPORT = "switchport";
    ACCESS = "access";
    NAMEIF = "nameif";

    ACCESS_LIST = "access-list";
    ACCESS_GROUP = "access-group";

    ADDRESS = "address";
    SECONDARY = "secondary";

    COMMUNITY_LIST = "community-list";

    PERMIT = "permit";
    DENY = "deny";

// protocols for 'permit' and 'deny' commands

    IP = "ip";
    ICMP = "icmp";
    TCP  = "tcp";
    UDP  = "udp";

//     AHP = "ahp";
//     EIGRP = "eigrp";
//     ESP = "esp";
//     GRE = "gre";
//     IGMP = "igmp";
//     IGRP = "igrp";
//     IPINIP = "ipinip";
//     NOS = "nos";
//     OSPF = "ospf";
//     PCP = "pcp";
//     PIM = "pim";

    HOST = "host";
    ANY  = "any";

    P_EQ = "eq";
    P_GT = "gt";
    P_LT = "lt";
    P_NEQ = "neq";
    P_RANGE = "range";

    LOG = "log";
    LOG_INPUT = "log-input";

    ESTABLISHED = "established";
    FRAGMENTS = "fragments";
    TIME_RANGE = "time-range";

    EXTENDED = "extended" ;
    STANDARD = "standard" ;

    PIX_WORD = "PIX" ;
    ASA_WORD = "ASA" ;
    VERSION_WORD = "Version" ;
}


LINE_COMMENT : "!" (~('\r' | '\n'))* NEWLINE ;

// This is for lines like these that appear at the top of "show run"
// : Saved
// :

COLON_COMMENT : COLON (~('\r' | '\n'))* NEWLINE ;

Whitespace :  ( '\003'..'\010' | '\t' | '\013' | '\f' | '\016'.. '\037' | '\177'..'\377' | ' ' )
        { _ttype = ANTLR_USE_NAMESPACE(antlr)Token::SKIP;  } ;


//COMMENT_START : '!' ;

NEWLINE : ( "\r\n" | '\r' | '\n' ) { newline(); } ;

protected
INT_CONST:;

protected
HEX_CONST:;

protected
NEG_INT_CONST:;

protected
DIGIT : '0'..'9'  ;

protected
HEXDIGIT : '0'..'9' | 'A'..'F' ;

NUMBER : 
		(
            ( (DIGIT)+ DOT (DIGIT)+ DOT (DIGIT)+ )=> ( (DIGIT)+ DOT (DIGIT)+ DOT (DIGIT)+ DOT (DIGIT)+ )
            { _ttype = IPV4; }
		|
            ( (DIGIT)+ DOT (DIGIT)+ )=> ( (DIGIT)+ DOT (DIGIT)+ )
        |
            ( DIGIT )+ { _ttype = INT_CONST; }
		|
            ( '0' 'x' ( HEXDIGIT )+ )  { _ttype = HEX_CONST; }   
        )
    ;

WORD : ( 'a'..'z' | 'A'..'Z' | '$' ) ( '!'..'/' | '0'..'9' | ':' | ';' | '<' | '=' | '>' | '?' | '@' | 'A'..'Z' | '\\' | '^' | '_' | '`' | 'a'..'z' )*
    ;

STRING : '"' (~'"')* '"';

PIPE_CHAR : '|';
NUMBER_SIGN : '#' ;
// DOLLAR : '$' ;
PERCENT : '%' ;
AMPERSAND : '&' ;
APOSTROPHE : '\'' ;
OPENING_PAREN : '(' ;
CLOSING_PAREN : ')' ;
STAR : '*' ;
PLUS : '+' ;
COMMA : ',' ;
MINUS : '-' ;
DOT : '.' ;
SLASH : '/' ;

COLON : ':' ;
SEMICOLON : ';' ;
LESS_THAN : '<' ;
EQUALS : '=' ;
GREATER_THAN : '>' ;
QUESTION : '?' ;
COMMERCIAL_AT : '@' ;

OPENING_SQUARE : '[' ;
CLOSING_SQUARE : ']' ;
CARET : '^' ;
UNDERLINE : '_' ;

OPENING_BRACE : '{' ;
CLOSING_BRACE : '}' ;
TILDE : '~' ;
