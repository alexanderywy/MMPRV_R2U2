find . -name "*.pdf" | while read pdf;do
	pdfcrop --margins '0 0 0 0' "$pdf" "$pdf"
done