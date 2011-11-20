;;;
;;; gauche.lazy - Lazy sequences
;;;  
;;;   Copyright (c) 2011  Shiro Kawai  <shiro@acm.org>
;;;   
;;;   Redistribution and use in source and binary forms, with or without
;;;   modification, are permitted provided that the following conditions
;;;   are met:
;;;   
;;;   1. Redistributions of source code must retain the above copyright
;;;      notice, this list of conditions and the following disclaimer.
;;;  
;;;   2. Redistributions in binary form must reproduce the above copyright
;;;      notice, this list of conditions and the following disclaimer in the
;;;      documentation and/or other materials provided with the distribution.
;;;  
;;;   3. Neither the name of the authors nor the names of its contributors
;;;      may be used to endorse or promote products derived from this
;;;      software without specific prior written permission.
;;;  
;;;   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
;;;   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
;;;   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
;;;   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
;;;   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
;;;   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
;;;   TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
;;;   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
;;;   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
;;;   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
;;;   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;;  

;; The primivites lseq and lrange are in src/scmlib.scm.

(define-module gauche.lazy
  (use gauche.generator)
  (export lmap lfilter))
(select-module gauche.lazy)

(define lmap
  (case-lambda
    [(proc arg)
     (define (g)
       (if (null? arg) (eof-object) (proc (pop! arg))))
     (generator->lseq g)]
    [(proc arg . more)
     (let1 args (cons arg more)
       (define (g)
         (if args
           (receive (cars cdrs)
               ((with-module gauche.internal %zip-nary-args) args)
             (set! args cdrs)
             (if cars
               (apply proc cars)
               (eof-object)))
           (eof-object)))
       (generator->lseq g))]))

;; NB: Should we define all l* variations corresponds to g* variations?
;; The list->generator portion smells bad.  Maybe g* variation should
;; coerce input sequences into generators automatically.
(define (lfilter fn seq) (generator->lseq (gfilter fn (list->generator seq))))

