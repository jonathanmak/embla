BEGIN { lastempty=0; nest=0 }
$0=="" { lastempty=FNR }
{
  idx=1;
  len=length( $0 );
  while( idx <= len ) {
    s = substr( $0, idx, 1 );
    if( s=="{" ) {
       nest++;
       if( nest==1 ) {
         funstart=lastempty+1;
       }
    } else if( s=="}" ) {
       nest--;
       if( nest==0 && FNR-funstart <= 100 ) {
         print FILENAME " " funstart " " FNR
       }
    }
    idx++;
  }
}
