;;;
;;; gauche.lazy - Lazy sequences
;;;
;;;   Copyright (c) 2011-2020  Shiro Kawai  <shiro@acm.org>
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

;; The primivites generator->lseq, lrange and lcons are supported in
;; the core.  See src/liblazy.scm.

;; The tests of this module is in ext/gauche, for this depends on
;; gauche.generator, which depends on modules built in ext/gauche.

(define-module gauche.lazy
  (use srfi-1)
  (use gauche.record)
  (use gauche.generator)
  (export x->lseq lunfold literate
          lmap lmap-accum lappend lappend-map lconcatenate
          linterweave lfilter lfilter-map lstate-filter
          ltake ltake-while lrxmatch lslices
          lseq->list
          generator->lseq/position port->char-lseq/position lseq-position
          <sequence-position> sequence-position?
          sequence-position-source sequence-position-line
          sequence-position-column sequence-position-item-count))
(select-module gauche.lazy)

;; Universal coercer.
;; This does not force OBJ if it is alreay an lseq.
;; NB: Putting specialized paths for vectors and strings may be a good idea
;; for performance.  We'll see.
(define (x->lseq obj)
  (cond [(null? obj) '()]
        [(eq? (class-of obj) <pair>) obj]
        [else
         (let1 g (x->generator obj)
           (if (applicable? g)
             (generator->lseq g)
             (error "cannot coerce the argument to a lazy sequence" obj)))]))

(define (lunfold p f g seed :optional (tail #f))
  ($ generator->lseq
     $ gunfold p f g seed (if tail (^s (list->generator (tail s))) #f)))

(define (literate f seed)
  (generator->lseq seed (giterate1 f seed)))

(define lmap
  (case-lambda
    [(proc arg)
     (let1 arg (x->lseq arg)
       (generator->lseq (^[](if (null? arg) (eof-object) (proc (pop! arg))))))]
    [(proc arg . more)
     (let1 args (map x->lseq (cons arg more))
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

(define lappend
  (case-lambda
    [() '()]
    [(arg) (x->lseq arg)]
    [args
     (generator->lseq
      (rec (gen)
        (cond [(null? args) (eof-object)]
              [(null? (car args)) (pop! args) (gen)]
              [else (pop! (car args))])))]))

;; Like (apply lappend seqs), but SEQS itself can be a (possibly infinite)
;; lazy seq.
(define (lconcatenate lseqs)
  (define cur #f)
  (if (null? lseqs)
    '()
    (generator->lseq
     (rec (gen)
       (cond [cur (let1 elt (cur)
                    (if (eof-object? elt)
                      (begin (set! cur #f) (gen))
                      elt))]
             [(null? lseqs) (eof-object)]
             [else (set! cur (x->generator (pop! lseqs))) (gen)])))))

;; Could be (lconcatenate (lmap proc args ...)), but this one doesn't
;; call proc on next item until the previous result is about to exhaust.
(define (lappend-map proc arg . args)
  (generator->lseq (gflatten (apply gmap proc arg args))))

;; (linterweave '(1 2 3 ...) '(a b c ...)) => (1 a 2 b 3 c ...)
;; Continues until all elements are exhausted.
(define (linterweave . seqs)
  (define (get-cars!)
    (let loop ([ss seqs] [rcars '()] [rcdrs '()])
      (cond [(null? ss) (set! seqs (reverse rcdrs)) (reverse rcars)]
            [(null? (car ss)) (eof-object)]
            [else
             (loop (cdr ss) (cons (caar ss) rcars) (cons (cdar ss) rcdrs))])))
  (if (null? seqs)
    '()
    (let1 cars (get-cars!)
      (if (eof-object? cars)
        '()
        (generator->lseq
         (rec (gen)
           (if (null? cars)
             (begin
               (set! cars (get-cars!))
               (if (eof-object? cars)
                 cars
                 (gen)))
             (pop! cars))))))))

;; NB: Should we define all l* variations corresponds to g* variations?
(define (lmap-accum fn seed seq . args)
  (generator->lseq (apply gmap-accum fn seed seq args)))
(define (lfilter fn seq) (generator->lseq (gfilter fn seq)))
(define (lfilter-map fn seq . args)
  (generator->lseq (apply gfilter-map fn seq args)))
(define (lstate-filter fn seed seq)
  (generator->lseq (gstate-filter fn seed seq)))
(define (ltake seq n :optional (fill? #f) (padding #f))
  (generator->lseq (gtake seq n fill? padding)))
;; ldrop is unnecessary
(define (ltake-while pred seq)
  (generator->lseq (gtake-while pred seq)))
;; ldrop-while is uncessary
(define (lrxmatch rx seq)
  (generator->lseq (grxmatch rx seq)))
(define (lslices seq k :optional (fill? #f) (padding #f))
  (generator->lseq (gslices seq k fill? padding)))

;; Realize all elements
;;   Use length+ to allow improper or circular lists.
(define (lseq->list s) (length+ s) s)

;; EXPERIMENTAL
;; Lseq with 'position' attached in pair attributes

(define-record-type <sequence-position>
  ;; this constructor is only for internal use
  (%make-sequence-position source line column item-count)
  sequence-position?
  (source     sequence-position-source)
  (line       sequence-position-line)
  (column     sequence-position-column)
  (item-count sequence-position-item-count))

(define (generator->lseq/position char-gen :key (source-name #f)
                                                (start-line 1)
                                                (start-column 1)
                                                (start-item-count 0))
  ;; The treemap of the tracker keeps integer-char-pos -> line-number
  ;; for characters at the beginning of line.  It is pointed from the cdr
  ;; of the input-char-position pair attribute.
  (define tracker (cons (make-tree-map) source-name))
  (define line-count start-line)
  (define char-count 0)
  (define eol #f)                       ;#t after #\newline is seen
  (define (gen)
    (glet1 ch (char-gen)
      (let ([pos char-count])
        (inc! char-count)
        (when eol
          (inc! line-count)
          (tree-map-put! (car tracker) pos line-count)
          (set! eol #f))
        (when (eqv? ch #\newline)
          (set! eol #t))
        (values ch `((input-position . (,pos . ,tracker)))))))
  (tree-map-put! (car tracker) 
                 (+ (- start-item-count start-column) 1)
                 start-line)
  (generator->lseq gen))

;; Actually 's' doesn't need to be a lseq.  Better name?
(define (lseq-position s)
  (and (pair? s)
       (and-let1 p (pair-attribute-get s 'input-position #f)
         (let ([pos (car p)]
               [src (cddr p)])
           (receive (bol-pos line) (tree-map-floor (cadr p) pos)
             (%make-sequence-position src line (+ 1 (- pos bol-pos)) pos))))))

(define (port->char-lseq/position :optional (port (current-input-port))
                                  :key (source-name #f)
                                       (start-line 1)
                                       (start-column 1)
                                       (start-item-count 0)
                                  :rest keys)
  (let1 name (or source-name (port-name port))
    (apply generator->lseq/position (cut read-char port)
           :source-name name (delete-keyword :source-name keys))))
